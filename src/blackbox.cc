// -*- mode: C++; indent-tabs-mode: nil; c-basic-offset: 2; -*-

#ifdef    HAVE_CONFIG_H
#  include "../config.h"
#endif // HAVE_CONFIG_H

extern "C" {
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <X11/cursorfont.h>
#include <X11/keysym.h>

#ifdef    SHAPE
#include <X11/extensions/shape.h>
#endif // SHAPE

#ifdef    HAVE_STDIO_H
#  include <stdio.h>
#endif // HAVE_STDIO_H

#ifdef HAVE_STDLIB_H
#  include <stdlib.h>
#endif // HAVE_STDLIB_H

#ifdef HAVE_STRING_H
#  include <string.h>
#endif // HAVE_STRING_H

#ifdef    HAVE_UNISTD_H
#  include <sys/types.h>
#  include <unistd.h>
#endif // HAVE_UNISTD_H

#ifdef    HAVE_SYS_PARAM_H
#  include <sys/param.h>
#endif // HAVE_SYS_PARAM_H

#ifdef    HAVE_SYS_SELECT_H
#  include <sys/select.h>
#endif // HAVE_SYS_SELECT_H

#ifdef    HAVE_SIGNAL_H
#  include <signal.h>
#endif // HAVE_SIGNAL_H

#ifdef    HAVE_SYS_SIGNAL_H
#  include <sys/signal.h>
#endif // HAVE_SYS_SIGNAL_H

#ifdef    HAVE_SYS_STAT_H
#  include <sys/types.h>
#  include <sys/stat.h>
#endif // HAVE_SYS_STAT_H

#ifdef    TIME_WITH_SYS_TIME
#  include <sys/time.h>
#  include <time.h>
#else // !TIME_WITH_SYS_TIME
#  ifdef    HAVE_SYS_TIME_H
#    include <sys/time.h>
#  else // !HAVE_SYS_TIME_H
#    include <time.h>
#  endif // HAVE_SYS_TIME_H
#endif // TIME_WITH_SYS_TIME

#ifdef    HAVE_LIBGEN_H
#  include <libgen.h>
#endif // HAVE_LIBGEN_H
}

#include <assert.h>

#include <algorithm>
#include <string>
using std::string;

#include "blackbox.hh"
#include "otk/gccache.hh"
#include "otk/image.hh"
#include "otk/assassin.hh"
#include "screen.hh"
#include "otk/util.hh"
#include "bbwindow.hh"
#include "workspace.hh"

namespace ob {

Blackbox *blackbox;


Blackbox::Blackbox(int argc, char **m_argv, char *rc)
  : Openbox(argc, m_argv) {

  if (! XSupportsLocale())
    fprintf(stderr, "X server does not support locale\n");

  if (XSetLocaleModifiers("") == NULL)
    fprintf(stderr, "cannot set locale modifiers\n");

  ob::blackbox = this;
  argv = m_argv;

  // try to make sure the ~/.openbox directory exists
  mkdir(otk::expandTilde("~/.openbox").c_str(), S_IREAD | S_IWRITE | S_IEXEC |
                                           S_IRGRP | S_IWGRP | S_IXGRP |
                                           S_IROTH | S_IWOTH | S_IXOTH);
  
  if (! rc) rc = "~/.openbox/rc3";
  rc_file = otk::expandTilde(rc);
  config.setFile(rc_file);  

  no_focus = False;

  resource.auto_raise_delay.tv_sec = resource.auto_raise_delay.tv_usec = 0;

  active_screen = 0;
  focused_window = changing_window = (BlackboxWindow *) 0;

  load_rc();

  xatom = new otk::OBProperty();

  cursor.session = XCreateFontCursor(otk::OBDisplay::display, XC_left_ptr);
  cursor.move = XCreateFontCursor(otk::OBDisplay::display, XC_fleur);
  cursor.ll_angle = XCreateFontCursor(otk::OBDisplay::display, XC_ll_angle);
  cursor.lr_angle = XCreateFontCursor(otk::OBDisplay::display, XC_lr_angle);
  cursor.ul_angle = XCreateFontCursor(otk::OBDisplay::display, XC_ul_angle);
  cursor.ur_angle = XCreateFontCursor(otk::OBDisplay::display, XC_ur_angle);

  for (int i = 0; i < ScreenCount(otk::OBDisplay::display); i++) {
    BScreen *screen = new BScreen(this, i);

    if (! screen->isScreenManaged()) {
      delete screen;
      continue;
    }

    screenList.push_back(screen);
  }

  if (screenList.empty()) {
    fprintf(stderr,
            "Blackbox::Blackbox: no managable screens found, aborting.\n");
    ::exit(3);
  }

  // save current settings and default values
  save_rc();

  // set the screen with mouse to the first managed screen
  active_screen = screenList.front();
  setFocusedWindow(0);

  XSynchronize(otk::OBDisplay::display, False);
  XSync(otk::OBDisplay::display, False);

  reconfigure_wait = False;

  timer = new otk::OBTimer(Openbox::instance->timerManager(),
                           (otk::OBTimeoutHandler)timeout,
                           this);
  timer->setTimeout(0l);
}


Blackbox::~Blackbox(void) {
  std::for_each(screenList.begin(), screenList.end(), otk::PointerAssassin());

  delete xatom;

  delete timer;
}


void Blackbox::process_event(XEvent *e) {
  switch (e->type) {
  case ButtonPress: {
    // strip the lock key modifiers
    //e->xbutton.state &= ~(NumLockMask | ScrollLockMask | LockMask);

    last_time = e->xbutton.time;

    BlackboxWindow *win = (BlackboxWindow *) 0;
    BScreen *scrn = (BScreen *) 0;

    if ((win = searchWindow(e->xbutton.window))) {
      win->buttonPressEvent(&e->xbutton);

      /* XXX: is this sane on low colour desktops? */
      if (e->xbutton.button == 1)
        win->installColormap(True);
    } else if ((scrn = searchScreen(e->xbutton.window))) {
      scrn->buttonPressEvent(&e->xbutton);
      if (active_screen != scrn) {
        active_screen = scrn;
        // first, set no focus window on the old screen
        setFocusedWindow(0);
        // and move focus to this screen
        setFocusedWindow(0);
      }
    }
    break;
  }

  case ButtonRelease: {
    // strip the lock key modifiers
    //e->xbutton.state &= ~(NumLockMask | ScrollLockMask | LockMask);

    last_time = e->xbutton.time;

    BlackboxWindow *win = (BlackboxWindow *) 0;

    if ((win = searchWindow(e->xbutton.window)))
      win->buttonReleaseEvent(&e->xbutton);

    break;
  }

  case ConfigureRequest: {
    BlackboxWindow *win = (BlackboxWindow *) 0;

    if ((win = searchWindow(e->xconfigurerequest.window))) {
      win->configureRequestEvent(&e->xconfigurerequest);
    } else {
      if (validateWindow(e->xconfigurerequest.window)) {
        XWindowChanges xwc;

        xwc.x = e->xconfigurerequest.x;
        xwc.y = e->xconfigurerequest.y;
        xwc.width = e->xconfigurerequest.width;
        xwc.height = e->xconfigurerequest.height;
        xwc.border_width = e->xconfigurerequest.border_width;
        xwc.sibling = e->xconfigurerequest.above;
        xwc.stack_mode = e->xconfigurerequest.detail;

        XConfigureWindow(otk::OBDisplay::display, e->xconfigurerequest.window,
                         e->xconfigurerequest.value_mask, &xwc);
      }
    }

    break;
  }

  case MapRequest: {
#ifdef    DEBUG
    fprintf(stderr, "Blackbox::process_event(): MapRequest for 0x%lx\n",
            e->xmaprequest.window);
#endif // DEBUG

    BlackboxWindow *win = searchWindow(e->xmaprequest.window);

    if (win) {
      bool focus = False;
      if (win->isIconic()) {
        win->deiconify();
        focus = True;
      }
      if (win->isShaded()) {
        win->shade();
        focus = True;
      }

      if (focus && (win->isTransient() || win->getScreen()->doFocusNew()) &&
          win->isVisible())
        win->setInputFocus();
    } else {
      BScreen *screen = searchScreen(e->xmaprequest.parent);

      if (! screen) {
        /*
          we got a map request for a window who's parent isn't root. this
          can happen in only one circumstance:

            a client window unmapped a managed window, and then remapped it
            somewhere between unmapping the client window and reparenting it
            to root.

          regardless of how it happens, we need to find the screen that
          the window is on
        */
        XWindowAttributes wattrib;
        if (! XGetWindowAttributes(otk::OBDisplay::display, e->xmaprequest.window,
                                   &wattrib)) {
          // failed to get the window attributes, perhaps the window has
          // now been destroyed?
          break;
        }

        screen = searchScreen(wattrib.root);
        assert(screen != 0); // this should never happen
      }

      screen->manageWindow(e->xmaprequest.window);
    }

    break;
  }

  case UnmapNotify: {
    BlackboxWindow *win = (BlackboxWindow *) 0;
    BScreen *screen = (BScreen *) 0;

    if ((win = searchWindow(e->xunmap.window))) {
      win->unmapNotifyEvent(&e->xunmap);
    } else if ((screen = searchSystrayWindow(e->xunmap.window))) {
      screen->removeSystrayWindow(e->xunmap.window);
    }

    break;
  }

  case DestroyNotify: {
    BlackboxWindow *win = (BlackboxWindow *) 0;
    BScreen *screen = (BScreen *) 0;
    BWindowGroup *group = (BWindowGroup *) 0;

    if ((win = searchWindow(e->xdestroywindow.window))) {
      win->destroyNotifyEvent(&e->xdestroywindow);
    } else if ((group = searchGroup(e->xdestroywindow.window))) {
      delete group;
    } else if ((screen = searchSystrayWindow(e->xunmap.window))) {
      screen->removeSystrayWindow(e->xunmap.window);
    }

    break;
  }

  case ReparentNotify: {
    /*
      this event is quite rare and is usually handled in unmapNotify
      however, if the window is unmapped when the reparent event occurs
      the window manager never sees it because an unmap event is not sent
      to an already unmapped window.
    */
    BlackboxWindow *win = searchWindow(e->xreparent.window);
    if (win)
      win->reparentNotifyEvent(&e->xreparent);
    break;
  }

  case MotionNotify: {
    // motion notify compression...
    XEvent realevent;
    unsigned int i = 0;
    while (XCheckTypedWindowEvent(otk::OBDisplay::display, e->xmotion.window,
                                  MotionNotify, &realevent)) {
      i++;
    }

    // if we have compressed some motion events, use the last one
    if ( i > 0 )
      e = &realevent;

    // the pointer is on the wrong screen
    if (! e->xmotion.same_screen)
      break;

    // strip the lock key modifiers
    //e->xmotion.state &= ~(NumLockMask | ScrollLockMask | LockMask);

    last_time = e->xmotion.time;

    BlackboxWindow *win = (BlackboxWindow *) 0;

    if ((win = searchWindow(e->xmotion.window)))
      win->motionNotifyEvent(&e->xmotion);

    break;
  }

  case PropertyNotify: {
    last_time = e->xproperty.time;

    BlackboxWindow *win = (BlackboxWindow *) 0;
    BScreen *screen = (BScreen *) 0;

    if ((win = searchWindow(e->xproperty.window)))
      win->propertyNotifyEvent(&e->xproperty);
    else if ((screen = searchScreen(e->xproperty.window)))
      screen->propertyNotifyEvent(&e->xproperty);
    break;
  }

  case EnterNotify: {
    last_time = e->xcrossing.time;

    BScreen *screen = (BScreen *) 0;
    BlackboxWindow *win = (BlackboxWindow *) 0;

    if (e->xcrossing.mode == NotifyGrab) break;

    if ((e->xcrossing.window == e->xcrossing.root) &&
        (screen = searchScreen(e->xcrossing.window))) {
      screen->getImageControl()->installRootColormap();
    } else if ((win = searchWindow(e->xcrossing.window))) {
      if (! no_focus)
        win->enterNotifyEvent(&e->xcrossing);
    }
    break;
  }

  case LeaveNotify: {
    last_time = e->xcrossing.time;

    BlackboxWindow *win = (BlackboxWindow *) 0;

    if ((win = searchWindow(e->xcrossing.window)))
      win->leaveNotifyEvent(&e->xcrossing);
    break;
  }

  case Expose: {
    // compress expose events
    XEvent realevent;
    unsigned int i = 0;
    int ex1, ey1, ex2, ey2;
    ex1 = e->xexpose.x;
    ey1 = e->xexpose.y;
    ex2 = ex1 + e->xexpose.width - 1;
    ey2 = ey1 + e->xexpose.height - 1;
    while (XCheckTypedWindowEvent(otk::OBDisplay::display, e->xexpose.window,
                                  Expose, &realevent)) {
      i++;

      // merge expose area
      ex1 = std::min(realevent.xexpose.x, ex1);
      ey1 = std::min(realevent.xexpose.y, ey1);
      ex2 = std::max(realevent.xexpose.x + realevent.xexpose.width - 1, ex2);
      ey2 = std::max(realevent.xexpose.y + realevent.xexpose.height - 1, ey2);
    }
    if ( i > 0 )
      e = &realevent;

    // use the merged area
    e->xexpose.x = ex1;
    e->xexpose.y = ey1;
    e->xexpose.width = ex2 - ex1 + 1;
    e->xexpose.height = ey2 - ey1 + 1;

    BlackboxWindow *win = (BlackboxWindow *) 0;

    if ((win = searchWindow(e->xexpose.window)))
      win->exposeEvent(&e->xexpose);

    break;
  }

  case KeyPress: {
    break;
  }

  case ColormapNotify: {
    BScreen *screen = searchScreen(e->xcolormap.window);

    if (screen)
      screen->setRootColormapInstalled((e->xcolormap.state ==
                                        ColormapInstalled) ? True : False);

    break;
  }

  case FocusIn: {
    if (e->xfocus.detail != NotifyNonlinear &&
        e->xfocus.detail != NotifyAncestor) {
      /*
        don't process FocusIns when:
        1. the new focus window isn't an ancestor or inferior of the old
        focus window (NotifyNonlinear)
        make sure to allow the FocusIn when the old focus window was an
        ancestor but didn't have a parent, such as root (NotifyAncestor)
      */
      break;
    }

    BlackboxWindow *win = searchWindow(e->xfocus.window);
    if (win) {
      if (! win->isFocused())
        win->setFocusFlag(True);

      /*
        set the event window to None.  when the FocusOut event handler calls
        this function recursively, it uses this as an indication that focus
        has moved to a known window.
      */
      e->xfocus.window = None;

      no_focus = False;   // focusing is back on
    }

    break;
  }

  case FocusOut: {
    if (e->xfocus.detail != NotifyNonlinear) {
      /*
        don't process FocusOuts when:
        2. the new focus window isn't an ancestor or inferior of the old
        focus window (NotifyNonlinear)
      */
      break;
    }

    BlackboxWindow *win = searchWindow(e->xfocus.window);
    if (win && win->isFocused()) {
      /*
        before we mark "win" as unfocused, we need to verify that focus is
        going to a known location, is in a known location, or set focus
        to a known location.
      */

      XEvent event;
      // don't check the current focus if FocusOut was generated during a grab
      bool check_focus = (e->xfocus.mode == NotifyNormal);

      /*
        First, check if there is a pending FocusIn event waiting.  if there
        is, process it and determine if focus has moved to another window
        (the FocusIn event handler sets the window in the event
        structure to None to indicate this).
      */
      if (XCheckTypedEvent(otk::OBDisplay::display, FocusIn, &event)) {

        process_event(&event);
        if (event.xfocus.window == None) {
          // focus has moved
          check_focus = False;
        }
      }

      if (check_focus) {
        /*
          Second, we query the X server for the current input focus.
          to make sure that we keep a consistent state.
        */
        BlackboxWindow *focus;
        Window w;
        int revert;
        XGetInputFocus(otk::OBDisplay::display, &w, &revert);
        focus = searchWindow(w);
        if (focus) {
          /*
            focus got from "win" to "focus" under some very strange
            circumstances, and we need to make sure that the focus indication
            is correct.
          */
          setFocusedWindow(focus);
        } else {
          // we have no idea where focus went... so we set it to somewhere
          setFocusedWindow(0);
        }
      }
    }

    break;
  }

  case ClientMessage: {
    if (e->xclient.format == 32) {
      if (e->xclient.message_type == xatom->atom(otk::OBProperty::wm_change_state)) {
        // WM_CHANGE_STATE message
        BlackboxWindow *win = searchWindow(e->xclient.window);
        if (! win || ! win->validateClient()) return;

        if (e->xclient.data.l[0] == IconicState)
          win->iconify();
        if (e->xclient.data.l[0] == NormalState)
          win->deiconify();
      } else if (e->xclient.message_type == 
                 xatom->atom(otk::OBProperty::blackbox_change_workspace) || 
                 e->xclient.message_type == 
                 xatom->atom(otk::OBProperty::net_current_desktop)) {
        // NET_CURRENT_DESKTOP message
        BScreen *screen = searchScreen(e->xclient.window);

        unsigned int workspace = e->xclient.data.l[0];
        if (screen && workspace < screen->getWorkspaceCount())
          screen->changeWorkspaceID(workspace);
      } else if (e->xclient.message_type == 
                 xatom->atom(otk::OBProperty::blackbox_change_window_focus)) {
        // TEMP HACK TO KEEP BBKEYS WORKING
        BlackboxWindow *win = searchWindow(e->xclient.window);

        if (win && win->isVisible() && win->setInputFocus())
          win->installColormap(True);
      } else if (e->xclient.message_type == 
                 xatom->atom(otk::OBProperty::net_active_window)) {
        // NET_ACTIVE_WINDOW
        BlackboxWindow *win = searchWindow(e->xclient.window);

        if (win) {
          BScreen *screen = win->getScreen();

          if (win->isIconic())
            win->deiconify(False, False);
          if (! win->isStuck() &&
              (win->getWorkspaceNumber() != screen->getCurrentWorkspaceID())) {
            no_focus = True;
            screen->changeWorkspaceID(win->getWorkspaceNumber());
          }
          if (win->isVisible() && win->setInputFocus()) {
            win->getScreen()->getWorkspace(win->getWorkspaceNumber())->
              raiseWindow(win);
            win->installColormap(True);
          }
        }
      } else if (e->xclient.message_type == 
                 xatom->atom(otk::OBProperty::blackbox_cycle_window_focus)) {
        // BLACKBOX_CYCLE_WINDOW_FOCUS
        BScreen *screen = searchScreen(e->xclient.window);

        if (screen) {
          if (! e->xclient.data.l[0])
            screen->prevFocus();
          else
            screen->nextFocus();
        }
      } else if (e->xclient.message_type == 
                 xatom->atom(otk::OBProperty::net_wm_desktop)) {
        // NET_WM_DESKTOP
        BlackboxWindow *win = searchWindow(e->xclient.window);

        if (win) {
          BScreen *screen = win->getScreen();
          unsigned long wksp = (unsigned) e->xclient.data.l[0];
          if (wksp < screen->getWorkspaceCount()) {
            if (win->isIconic()) win->deiconify(False, True);
            if (win->isStuck()) win->stick();
            if (wksp != screen->getCurrentWorkspaceID())
              win->withdraw();
            else
              win->show();
            screen->reassociateWindow(win, wksp, True);
          } else if (wksp == 0xfffffffe || // XXX: BUG, BUT DOING THIS SO KDE WORKS FOR NOW!!
                     wksp == 0xffffffff) {
            if (win->isIconic()) win->deiconify(False, True);
            if (! win->isStuck()) win->stick();
            if (! win->isVisible()) win->show();
          }
        }
      } else if (e->xclient.message_type == 
                 xatom->atom(otk::OBProperty::blackbox_change_attributes)) {
        // BLACKBOX_CHANGE_ATTRIBUTES
        BlackboxWindow *win = searchWindow(e->xclient.window);

        if (win && win->validateClient()) {
          BlackboxHints net;
          net.flags = e->xclient.data.l[0];
          net.attrib = e->xclient.data.l[1];
          net.workspace = e->xclient.data.l[2];
          net.stack = e->xclient.data.l[3];
          net.decoration = e->xclient.data.l[4];

          win->changeBlackboxHints(&net);
        }
      } else if (e->xclient.message_type == 
                xatom->atom(otk::OBProperty::net_number_of_desktops)) {
        // NET_NUMBER_OF_DESKTOPS
        BScreen *screen = searchScreen(e->xclient.window);
        
        if (e->xclient.data.l[0] > 0)
          screen->changeWorkspaceCount((unsigned) e->xclient.data.l[0]);
      } else if (e->xclient.message_type ==
                 xatom->atom(otk::OBProperty::net_close_window)) {
        // NET_CLOSE_WINDOW
        BlackboxWindow *win = searchWindow(e->xclient.window);
        if (win && win->validateClient())
          win->close(); // could this be smarter?
      } else if (e->xclient.message_type ==
                 xatom->atom(otk::OBProperty::net_wm_moveresize)) {
        // NET_WM_MOVERESIZE
        BlackboxWindow *win = searchWindow(e->xclient.window);
        if (win && win->validateClient()) {
          int x_root = e->xclient.data.l[0],
              y_root = e->xclient.data.l[1];
          if ((Atom) e->xclient.data.l[2] ==
              xatom->atom(otk::OBProperty::net_wm_moveresize_move)) {
            win->beginMove(x_root, y_root);
          } else {
            if ((Atom) e->xclient.data.l[2] ==
                xatom->atom(otk::OBProperty::net_wm_moveresize_size_topleft))
              win->beginResize(x_root, y_root, BlackboxWindow::TopLeft);
            else if ((Atom) e->xclient.data.l[2] ==
                     xatom->atom(otk::OBProperty::net_wm_moveresize_size_topright))
              win->beginResize(x_root, y_root, BlackboxWindow::TopRight);
            else if ((Atom) e->xclient.data.l[2] ==
                     xatom->atom(otk::OBProperty::net_wm_moveresize_size_bottomleft))
              win->beginResize(x_root, y_root, BlackboxWindow::BottomLeft);
            else if ((Atom) e->xclient.data.l[2] ==
                xatom->atom(otk::OBProperty::net_wm_moveresize_size_bottomright))
              win->beginResize(x_root, y_root, BlackboxWindow::BottomRight);
          }
        }
      } else if (e->xclient.message_type ==
                 xatom->atom(otk::OBProperty::net_wm_state)) {
        // NET_WM_STATE
        BlackboxWindow *win = searchWindow(e->xclient.window);
        if (win && win->validateClient()) {
          const Atom action = (Atom) e->xclient.data.l[0];
          const Atom state[] = { (Atom) e->xclient.data.l[1],
                                 (Atom) e->xclient.data.l[2] };
          
          for (int i = 0; i < 2; ++i) {
            if (! state[i])
              continue;

            if ((Atom) e->xclient.data.l[0] == 1) {
              // ADD
              if (state[i] == xatom->atom(otk::OBProperty::net_wm_state_modal)) {
                win->setModal(True);
              } else if (state[i] ==
                         xatom->atom(otk::OBProperty::net_wm_state_maximized_vert)) {
                if (win->isMaximizedHoriz()) {
                  win->maximize(0); // unmaximize
                  win->maximize(1); // full
                } else if (! win->isMaximized()) {
                  win->maximize(2); // vert
                }
              } else if (state[i] ==
                         xatom->atom(otk::OBProperty::net_wm_state_maximized_horz)) {
                if (win->isMaximizedVert()) {
                  win->maximize(0); // unmaximize
                  win->maximize(1); // full
                } else if (! win->isMaximized()) {
                  win->maximize(3); // horiz
                }
              } else if (state[i] ==
                         xatom->atom(otk::OBProperty::net_wm_state_shaded)) {
                if (! win->isShaded())
                  win->shade();
              } else if (state[i] ==
                         xatom->atom(otk::OBProperty::net_wm_state_skip_taskbar)) {
                win->setSkipTaskbar(True);
              } else if (state[i] ==
                         xatom->atom(otk::OBProperty::net_wm_state_skip_pager)) {
                win->setSkipPager(True);
              } else if (state[i] ==
                         xatom->atom(otk::OBProperty::net_wm_state_fullscreen)) {
                win->setFullscreen(True);
              }
            } else if (action == 0) {
              // REMOVE
              if (state[i] == xatom->atom(otk::OBProperty::net_wm_state_modal)) {
                win->setModal(False);
              } else if (state[i] ==
                         xatom->atom(otk::OBProperty::net_wm_state_maximized_vert)) {
                if (win->isMaximizedFull()) {
                  win->maximize(0); // unmaximize
                  win->maximize(3); // horiz
                } else if (win->isMaximizedVert()) {
                  win->maximize(0); // unmaximize
                }
              } else if (state[i] ==
                         xatom->atom(otk::OBProperty::net_wm_state_maximized_horz)) {
                if (win->isMaximizedFull()) {
                  win->maximize(0); // unmaximize
                  win->maximize(2); // vert
                } else if (win->isMaximizedHoriz()) {
                  win->maximize(0); // unmaximize
                }
              } else if (state[i] ==
                         xatom->atom(otk::OBProperty::net_wm_state_shaded)) {
                if (win->isShaded())
                  win->shade();
              } else if (state[i] ==
                         xatom->atom(otk::OBProperty::net_wm_state_skip_taskbar)) {
                win->setSkipTaskbar(False);
              } else if (state[i] ==
                         xatom->atom(otk::OBProperty::net_wm_state_skip_pager)) {
                win->setSkipPager(False);
              } else if (state[i] ==
                         xatom->atom(otk::OBProperty::net_wm_state_fullscreen)) {
                win->setFullscreen(False);
              }
            } else if (action == 2) {
              // TOGGLE
              if (state[i] == xatom->atom(otk::OBProperty::net_wm_state_modal)) {
                win->setModal(! win->isModal());
              } else if (state[i] ==
                         xatom->atom(otk::OBProperty::net_wm_state_maximized_vert)) {
                if (win->isMaximizedFull()) {
                  win->maximize(0); // unmaximize
                  win->maximize(3); // horiz
                } else if (win->isMaximizedVert()) {
                  win->maximize(0); // unmaximize
                } else if (win->isMaximizedHoriz()) {
                  win->maximize(0); // unmaximize
                  win->maximize(1); // full
                } else {
                  win->maximize(2); // vert
                }
              } else if (state[i] ==
                         xatom->atom(otk::OBProperty::net_wm_state_maximized_horz)) {
                if (win->isMaximizedFull()) {
                  win->maximize(0); // unmaximize
                  win->maximize(2); // vert
                } else if (win->isMaximizedHoriz()) {
                  win->maximize(0); // unmaximize
                } else if (win->isMaximizedVert()) {
                  win->maximize(0); // unmaximize
                  win->maximize(1); // full
                } else {
                  win->maximize(3); // horiz
                }
              } else if (state[i] ==
                         xatom->atom(otk::OBProperty::net_wm_state_shaded)) {
                win->shade();
              } else if (state[i] ==
                         xatom->atom(otk::OBProperty::net_wm_state_skip_taskbar)) {
                win->setSkipTaskbar(! win->skipTaskbar());
              } else if (state[i] ==
                         xatom->atom(otk::OBProperty::net_wm_state_skip_pager)) {
                win->setSkipPager(! win->skipPager());
              } else if (state[i] ==
                         xatom->atom(otk::OBProperty::net_wm_state_fullscreen)) {
                win->setFullscreen(! win->isFullscreen());
              }
            }
          }
        }
      }
    }

    break;
  }

  case NoExpose:
  case ConfigureNotify:
  case MapNotify:
    break; // not handled, just ignore

  default: {
#ifdef    SHAPE
    if (e->type == getShapeEventBase()) {
      XShapeEvent *shape_event = (XShapeEvent *) e;
      BlackboxWindow *win = searchWindow(e->xany.window);

      if (win && shape_event->kind == ShapeBounding)
        win->shapeEvent(shape_event);
    }
#endif // SHAPE
  }
  } // switch
}


bool Blackbox::handleSignal(int sig) {
  switch (sig) {
  case SIGHUP:
    reconfigure();
    break;

  case SIGUSR1:
    restart();
    break;

  case SIGUSR2:
    break;

  case SIGPIPE:
  case SIGSEGV:
  case SIGFPE:
  case SIGINT:
  case SIGTERM:
    shutdown();

  default:
    return False;
  }

  return True;
}


bool Blackbox::validateWindow(Window window) {
  XEvent event;
  if (XCheckTypedWindowEvent(otk::OBDisplay::display, window, DestroyNotify, &event)) {
    XPutBackEvent(otk::OBDisplay::display, &event);

    return False;
  }

  return True;
}


BScreen *Blackbox::searchScreen(Window window) {
  ScreenList::iterator it = screenList.begin();

  for (; it != screenList.end(); ++it) {
    BScreen *s = *it;
    if (s->getRootWindow() == window)
      return s;
  }

  return (BScreen *) 0;
}


BScreen *Blackbox::searchSystrayWindow(Window window) {
  WindowScreenLookup::iterator it = systraySearchList.find(window);
  if (it != systraySearchList.end())
    return it->second;

  return (BScreen*) 0;
}


BlackboxWindow *Blackbox::searchWindow(Window window) {
  WindowLookup::iterator it = windowSearchList.find(window);
  if (it != windowSearchList.end())
    return it->second;

  return (BlackboxWindow*) 0;
}


BWindowGroup *Blackbox::searchGroup(Window window) {
  GroupLookup::iterator it = groupSearchList.find(window);
  if (it != groupSearchList.end())
    return it->second;

  return (BWindowGroup *) 0;
}


void Blackbox::saveSystrayWindowSearch(Window window, BScreen *screen) {
  systraySearchList.insert(WindowScreenLookupPair(window, screen));
}


void Blackbox::saveWindowSearch(Window window, BlackboxWindow *data) {
  windowSearchList.insert(WindowLookupPair(window, data));
}


void Blackbox::saveGroupSearch(Window window, BWindowGroup *data) {
  groupSearchList.insert(GroupLookupPair(window, data));
}


void Blackbox::removeSystrayWindowSearch(Window window) {
  systraySearchList.erase(window);
}


void Blackbox::removeWindowSearch(Window window) {
  windowSearchList.erase(window);
}


void Blackbox::removeGroupSearch(Window window) {
  groupSearchList.erase(window);
}


void Blackbox::restart(const char *prog) {
  shutdown();

  if (prog) {
    putenv(const_cast<char *>(screenList.front()->displayString().c_str()));
    execlp(prog, prog, NULL);
    perror(prog);
  }

  // fall back in case the above execlp doesn't work
  execvp(argv[0], argv);
  string name = ::basename(argv[0]);
  execvp(name.c_str(), argv);
}


void Blackbox::shutdown(void) {
  Openbox::shutdown();

  XSetInputFocus(otk::OBDisplay::display, PointerRoot, None, CurrentTime);

  std::for_each(screenList.begin(), screenList.end(),
                std::mem_fun(&BScreen::shutdown));

  XSync(otk::OBDisplay::display, False);
}


#ifdef    XINERAMA
void Blackbox::saveXineramaPlacement(bool x) {
  resource.xinerama_placement = x;
  config.setValue("session.xineramaSupport.windowPlacement",
                  resource.xinerama_placement);
  reconfigure();  // make sure all screens get this 

void Blackbox::saveXineramaMaximizing(bool x) {
  resource.xinerama_maximize = x;
  config.setValue("session.xineramaSupport.windowMaximizing",
                  resource.xinerama_maximize);
  reconfigure();  // make sure all screens get this change
}


void Blackbox::saveXineramaSnapping(bool x) {
  resource.xinerama_snap = x;
  config.setValue("session.xineramaSupport.windowSnapping",
                  resource.xinerama_snap);
  reconfigure();  // make sure all screens get this change
}
#endif // XINERAMA

  
/*
 * Save all values as they are so that the defaults will be written to the rc
 * file
 */
void Blackbox::save_rc(void) {
  config.setAutoSave(false);

  config.setValue("session.colorsPerChannel", resource.colors_per_channel);
  config.setValue("session.doubleClickInterval",
                  resource.double_click_interval);
  config.setValue("session.autoRaiseDelay",
                  ((resource.auto_raise_delay.tv_sec * 1000) +
                   (resource.auto_raise_delay.tv_usec / 1000)));
  config.setValue("session.cacheLife", resource.cache_life / 60000);
  config.setValue("session.cacheMax", resource.cache_max);
  config.setValue("session.styleFile", resource.style_file);
  config.setValue("session.titlebarLayout", resource.titlebar_layout);

  string s;
  if (resource.mod_mask & Mod1Mask) s += "Mod1-";
  if (resource.mod_mask & Mod2Mask) s += "Mod2-";
  if (resource.mod_mask & Mod3Mask) s += "Mod3-";
  if (resource.mod_mask & Mod4Mask) s += "Mod4-";
  if (resource.mod_mask & Mod5Mask) s += "Mod5-";
  if (resource.mod_mask & ShiftMask) s += "Shift-";
  if (resource.mod_mask & ControlMask) s += "Control-";
  s.resize(s.size() - 1); // drop the last '-'
  config.setValue("session.modifierMask", s);
  
#ifdef    XINERAMA
  saveXineramaPlacement(resource.xinerama_placement);
  saveXineramaMaximizing(resource.xinerama_maximize);
  saveXineramaSnapping(resource.xinerama_snap);
#endif // XINERAMA

  std::for_each(screenList.begin(), screenList.end(),
                std::mem_fun(&BScreen::save_rc));
 
  config.setAutoSave(true);
  config.save();
}


void Blackbox::load_rc(void) {
  if (! config.load())
    config.create();
  
  string s;

  if (! config.getValue("session.colorsPerChannel",
                        resource.colors_per_channel))
    resource.colors_per_channel = 4;
  if (resource.colors_per_channel < 2) resource.colors_per_channel = 2;
  else if (resource.colors_per_channel > 6) resource.colors_per_channel = 6;

  if (config.getValue("session.styleFile", s))
    resource.style_file = otk::expandTilde(s);
  else
    resource.style_file = DEFAULTSTYLE;

  if (! config.getValue("session.doubleClickInterval",
                       resource.double_click_interval));
    resource.double_click_interval = 250;

  if (! config.getValue("session.autoRaiseDelay",
                       resource.auto_raise_delay.tv_usec))
    resource.auto_raise_delay.tv_usec = 400;
  resource.auto_raise_delay.tv_sec = resource.auto_raise_delay.tv_usec / 1000;
  resource.auto_raise_delay.tv_usec -=
    (resource.auto_raise_delay.tv_sec * 1000);
  resource.auto_raise_delay.tv_usec *= 1000;

  if (! config.getValue("session.cacheLife", resource.cache_life))
    resource.cache_life = 5;
  resource.cache_life *= 60000;

  if (! config.getValue("session.cacheMax", resource.cache_max))
    resource.cache_max = 200;
  
  if (! config.getValue("session.titlebarLayout", resource.titlebar_layout))
    resource.titlebar_layout = "ILMC";

#ifdef    XINERAMA
  if (! config.getValue("session.xineramaSupport.windowPlacement",
                        resource.xinerama_placement))
    resource.xinerama_placement = false;

  if (! config.getValue("session.xineramaSupport.windowMaximizing",
                        resource.xinerama_maximize))
    resource.xinerama_maximize = false;

  if (! config.getValue("session.xineramaSupport.windowSnapping",
                        resource.xinerama_snap))
    resource.xinerama_snap = false;
#endif // XINERAMA
  
  resource.mod_mask = 0;
  if (config.getValue("session.modifierMask", s)) {
    if (s.find("Mod1") != string::npos)
      resource.mod_mask |= Mod1Mask;
    if (s.find("Mod2") != string::npos)
      resource.mod_mask |= Mod2Mask;
    if (s.find("Mod3") != string::npos)
      resource.mod_mask |= Mod3Mask;
    if (s.find("Mod4") != string::npos)
      resource.mod_mask |= Mod4Mask;
    if (s.find("Mod5") != string::npos)
      resource.mod_mask |= Mod5Mask;
    if (s.find("Shift") != string::npos)
      resource.mod_mask |= ShiftMask;
    if (s.find("Control") != string::npos)
      resource.mod_mask |= ControlMask;
  }
  if (! resource.mod_mask)
    resource.mod_mask = Mod1Mask;
}


void Blackbox::reconfigure(void) {
  // don't reconfigure while saving the initial rc file, it's a waste and it
  // breaks somethings (workspace names)
  if (state() == Openbox::State_Starting) return;

  reconfigure_wait = True;

  if (! timer->timing()) timer->start();
}


void Blackbox::real_reconfigure(void) {
  load_rc();
  
  otk::OBDisplay::gcCache()->purge();

  std::for_each(screenList.begin(), screenList.end(),
                std::mem_fun(&BScreen::reconfigure));
}


void Blackbox::saveStyleFilename(const string& filename) {
  assert(! filename.empty());
  resource.style_file = filename;
  config.setValue("session.styleFile", resource.style_file);
}


void Blackbox::timeout(Blackbox *t) {
  if (t->reconfigure_wait)
    t->real_reconfigure();

  t->reconfigure_wait = False;
}


void Blackbox::setChangingWindow(BlackboxWindow *win) {
  // make sure one of the two is null and the other isn't
  assert((! changing_window && win) || (! win && changing_window));
  changing_window = win;
}


void Blackbox::setFocusedWindow(BlackboxWindow *win) {
  if (focused_window && focused_window == win) // nothing to do
    return;

  BScreen *old_screen = 0;

  if (focused_window) {
    focused_window->setFocusFlag(False);
    old_screen = focused_window->getScreen();
  }

  if (win && ! win->isIconic()) {
    // the active screen is the one with the last focused window...
    // this will keep focus on this screen no matter where the mouse goes,
    // so multihead keybindings will continue to work on that screen until the
    // user focuses a window on a different screen.
    active_screen = win->getScreen();
    focused_window = win;
  } else {
    focused_window = 0;
    if (! old_screen) {
      if (active_screen) {
        // set input focus to the toolbar of the screen with mouse
        XSetInputFocus(otk::OBDisplay::display,
                       active_screen->getRootWindow(),
                       RevertToPointerRoot, CurrentTime);
      } else {
        // set input focus to the toolbar of the first managed screen
        XSetInputFocus(otk::OBDisplay::display,
                       screenList.front()->getRootWindow(),
                       RevertToPointerRoot, CurrentTime);
      }
    } else {
      // set input focus to the toolbar of the last screen
      XSetInputFocus(otk::OBDisplay::display, old_screen->getRootWindow(),
                     RevertToPointerRoot, CurrentTime);
    }
  }

  if (active_screen && active_screen->isScreenManaged()) {
    active_screen->updateNetizenWindowFocus();
  }

  if (old_screen && old_screen != active_screen) {
    old_screen->updateNetizenWindowFocus();
  }
}


} 
