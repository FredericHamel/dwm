/* See LICENSE file for copyright and license details.
 *
 * dynamic window manager is designed like any other X client as well. It is
 * driven through handling X events. In contrast to other X clients, a window
 * manager selects for SubstructureRedirectMask on the root window, to receive
 * events about window (dis-)appearance. Only one X connection at a time is
 * allowed to select for this event mask.
 *
 * The event handlers of dwm are organized in an array which is accessed
 * whenever a new event has been fetched. This allows event dispatching
 * in O(1) time.
 *
 * Each child of the root window is called a client, except windows which have
 * set the override_redirect flag. Clients are organized in a linked client
 * list on each monitor, the focus history is remembered through a stack list
 * on each monitor. Each client contains a bit array to indicate the tags of a
 * client.
 *
 * Keys and tagging rules are organized as arrays and defined in config.h.
 *
 * To understand everything else, start reading main().
 */
#include <errno.h>
#include <locale.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <X11/cursorfont.h>
#include <X11/keysym.h>
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xproto.h>
#include <X11/Xutil.h>
#ifdef XINERAMA
#include <X11/extensions/Xinerama.h>
#endif /* XINERAMA */
#include <X11/Xft/Xft.h>
#include <X11/Xlib-xcb.h>
#include <xcb/res.h>

#include "drw.h"
#include "util.h"

/* macros */
#define BUTTONMASK              (ButtonPressMask|ButtonReleaseMask)
#define CLEANMASK(mask)         (mask & ~(numlockmask|LockMask) & (ShiftMask|ControlMask|Mod1Mask|Mod2Mask|Mod3Mask|Mod4Mask|Mod5Mask))
#define GETINC(X)               ((X) - 2000)
#define INC(X)                  ((X) + 2000)
#define INTERSECT(x,y,w,h,m)    (MAX(0, MIN((x)+(w),(m)->wx+(m)->ww) - MAX((x),(m)->wx)) \
                               * MAX(0, MIN((y)+(h),(m)->wy+(m)->wh) - MAX((y),(m)->wy)))
#define ISINC(X)                ((X) > 1000 && (X) < 3000)
#define ISVISIBLE(C)            ((C->tags & C->mon->tagset[C->mon->seltags]))
#define LENGTH(X)               (sizeof X / sizeof X[0])
#define MOD(N,M)                ((N)%(M) < 0 ? (N)%(M) + (M) : (N)%(M))
#define MOUSEMASK               (BUTTONMASK|PointerMotionMask)
#define WIDTH(X)                ((X)->w + 2 * (X)->bw)
#define HEIGHT(X)               ((X)->h + 2 * (X)->bw)
#define TAGMASK                 ((1 << LENGTH(tags)) - 1)
#define TEXTW(X)                (drw_fontset_getwidth(drw, (X)) + lrpad)
#define TRUNC(X,A,B)            (MAX((A), MIN((X), (B))))

#define SYSTEM_TRAY_REQUEST_DOCK    0
#define _NET_SYSTEM_TRAY_ORIENTATION_HORZ 0

/* XEMBED messages */
#define XEMBED_EMBEDDED_NOTIFY      0
#define XEMBED_WINDOW_ACTIVATE      1
#define XEMBED_FOCUS_IN             4
#define XEMBED_MODALITY_ON         10

#define XEMBED_MAPPED              (1 << 0)
#define XEMBED_WINDOW_ACTIVATE      1
#define XEMBED_WINDOW_DEACTIVATE    2

#define VERSION_MAJOR               0
#define VERSION_MINOR               0
#define XEMBED_EMBEDDED_VERSION (VERSION_MAJOR << 16) | VERSION_MINOR

/* enums */
enum { CurNormal, CurResizeBottomRight, CurResizeBottomLeft, CurResizeTopRight, CurResizeTopLeft, CurMove, CurLast };        /* cursor */
enum { SchemeNorm, SchemeSel }; /* color schemes */
enum { NetSupported, NetSystemTray, NetSystemTrayOP, NetSystemTrayOrientation,
       NetWMName, NetWMState, NetWMFullscreen, NetActiveWindow, NetWMWindowType,
       NetWMCheck, NetWMWindowTypeDialog, NetClientList, NetLast }; /* EWMH atoms */
enum { Manager, Xembed, XembedInfo, XLast }; /* Xembed atoms */
enum { WMProtocols, WMDelete, WMState, WMTakeFocus, WMLast }; /* default atoms */
enum { ClkTagBar, ClkLtSymbol, ClkStatusText, ClkWinTitle,
       ClkClientWin, ClkRootWin, ClkLast }; /* clicks */
enum { DirHor, DirVer, DirRotHor, DirRotVer, DirLast }; /* tiling dirs */

typedef union {
  int i;
  unsigned int ui;
  float f;
  const void *v;
} Arg;

typedef struct {
  unsigned int x, y, fx, fy, n, dir;
  float fact;
} Area;

typedef struct {
  unsigned int click;
  unsigned int mask;
  unsigned int button;
  void (*func)(const Arg *arg);
  const Arg arg;
} Button;

typedef struct Monitor Monitor;
typedef struct Client Client;
struct Client {
  char name[256];
  float mina, maxa;
  int x, y, w, h;
  int oldx, oldy, oldw, oldh;
  int basew, baseh, incw, inch, maxw, maxh, minw, minh;
  int bw, oldbw;
  unsigned int tags;
  Bool isfixed, isfloating, isurgent, neverfocus, oldstate, isfullscreen, isterminal, noswallow;
  pid_t pid;
  Client *next;
  Client *snext;
  Client *swallowing;
  Monitor *mon;
  Window win;
};

typedef struct {
  unsigned int mod;
  KeySym keysym;
  void (*func)(const Arg *);
  const Arg arg;
} Key;

typedef struct {
  const char *symbol;
  void (*arrange)(Monitor *);
} Layout;

typedef struct Pertag Pertag;
struct Monitor {
  char ltsymbol[16];
  int nmaster;
  int num;
  int by;               /* bar geometry */
  int mx, my, mw, mh;   /* screen size */
  int wx, wy, ww, wh;   /* window area  */
  unsigned int seltags;
  unsigned int sellt;
  unsigned int tagset[2];
  Bool showbar;
  Bool topbar;
  Client *clients;
  Client *sel;
  Client *stack;
  Monitor *next;
  Window barwin;
  const Layout *lt[2];
  Pertag *pertag;
};

typedef struct {
  const char *class;
  const char *instance;
  const char *title;
  unsigned int tags;
  Bool isfloating;
  Bool isterminal;
  int noswallow;
  int monitor;
} Rule;

typedef struct Systray   Systray;
struct Systray {
  Window win;
  Client *icons;
};

/* function declarations */
static void applyrules(Client *c);
static Bool applysizehints(Client *c, int *x, int *y, int *w, int *h, Bool interact);
static void arrange(Monitor *m);
static void arrangemon(Monitor *m);
static void attach(Client *c);
static void attachstack(Client *c);
static void buttonpress(XEvent *e);
static void changemon(Monitor *m);
static void checkotherwm(void);
static void cleanup(void);
static void cleanupmon(Monitor *mon);
static void clientmessage(XEvent *e);
static void configure(Client *c);
static void configurenotify(XEvent *e);
static void configurerequest(XEvent *e);
static Monitor *createmon(void);
static void destroynotify(XEvent *e);
static void detach(Client *c);
static void detachstack(Client *c);
static Monitor *dirtomon(int dir);
static void drawbar(Monitor *m);
static void drawbars(void);
static void enternotify(XEvent *e);
static void expose(XEvent *e);
static void focus(Client *c);
static void focusin(XEvent *e);
static void focusmon(const Arg *arg);
static void focusstack(const Arg *arg);
static int getrootptr(int *x, int *y);
static Atom getatomprop(Client *c, Atom prop);
static Bool getrootptr(int *x, int *y);
static long getstate(Window w);
static unsigned int getsystraywidth();
static Bool gettextprop(Window w, Atom atom, char *text, unsigned int size);
static void grabbuttons(Client *c, Bool focused);
static void grabkeys(void);
static void grid(Monitor* m);
static void incnmaster(const Arg *arg);
static void keypress(XEvent *e);
static void killclient(const Arg *arg);
static void manage(Window w, XWindowAttributes *wa);
static void mappingnotify(XEvent *e);
static void maprequest(XEvent *e);
static void monocle(Monitor *m);
static void motionnotify(XEvent *e);
static void movemouse(const Arg *arg);
static void movestack(const Arg *arg);
static Client *nexttiled(Client *c);
static void pop(Client *);
static void propertynotify(XEvent *e);
static void quit(const Arg *arg);
static Monitor *recttomon(int x, int y, int w, int h);
static void restart(const Arg *arg);
static void removesystrayicon(Client *i);
static void resize(Client *c, int x, int y, int w, int h, Bool interact);
static void resizebarwin(Monitor *m);
static void resizeclient(Client *c, int x, int y, int w, int h);
static void resizemouse(const Arg *arg);
static void resizerequest(XEvent *e);
static void restack(Monitor *m);
static void run(void);
static void scan(void);
static Bool sendevent(Window w, Atom proto, int m, long d0, long d1, long d2, long d3, long d4);
static void sendmon(Client *c, Monitor *m);
static void setclientstate(Client *c, long state);
static void setdirs(const Arg *arg);
static void setfacts(const Arg *arg);
static void setfocus(Client *c);
static void setfullscreen(Client *c, Bool fullscreen);
static void setlayout(const Arg *arg);
static void setup(void);
static void seturgent(Client *c, Bool urg);
static void showhide(Client *c);
static void sigchld(int unused);
static void spawn(const Arg *arg);
static void tag(const Arg *arg);
static void tagmon(const Arg *arg);
static void tile(Monitor *);
static void togglebar(const Arg *arg);
static void togglefloating(const Arg *arg);
static void togglefullscreen(const Arg *arg);
static void toggletag(const Arg *arg);
static void toggleview(const Arg *arg);
static void unfocus(Client *c, Bool setfocus);
static void unmanage(Client *c, Bool destroyed);
static void unmapnotify(XEvent *e);
static void updatebarpos(Monitor *m);
static void updatebars(void);
static void updateclientlist(void);
static int updategeom(void);
static void updatenumlockmask(void);
static void updatesizehints(Client *c);
static void updatestatus(void);
static void updatesystray(void);
static void updatesystrayicongeom(Client *i, int w, int h);
static void updatesystrayiconstate(Client *i, XPropertyEvent *ev);
static void updatewindowtype(Client *c);
static void updatetitle(Client *c);
static void updatewmhints(Client *c);
static void view(const Arg *arg);
static Client *wintoclient(Window w);
static Monitor *wintomon(Window w);
static Client *wintosystrayicon(Window w);
static int xerror(Display *dpy, XErrorEvent *ee);
static int xerrordummy(Display *dpy, XErrorEvent *ee);
static int xerrorstart(Display *dpy, XErrorEvent *ee);
static void zoom(const Arg *arg);

static pid_t getparentprocess(pid_t p);
static int isdescprocess(pid_t p, pid_t c);
static Client *swallowingclient(Window w);
static Client *termforwin(const Client *c);
static pid_t winpid(Window w);

/* variables */
static Systray *systray = NULL;
static unsigned long systrayorientation = _NET_SYSTEM_TRAY_ORIENTATION_HORZ;
static const char broken[] = "broken";
static char stext[256];
static int scanner;
static int screen;
static int sw, sh;           /* X display screen geometry width, height */
static int bh, blw = 0;      /* bar geometry */
static int lrpad;            /* sum of left and right padding for text */
static int (*xerrorxlib)(Display *, XErrorEvent *);
static unsigned int numlockmask = 0;
static void (*handler[LASTEvent]) (XEvent *) = {
  [ButtonPress] = buttonpress,
  [ClientMessage] = clientmessage,
  [ConfigureRequest] = configurerequest,
  [ConfigureNotify] = configurenotify,
  [DestroyNotify] = destroynotify,
  [EnterNotify] = enternotify,
  [Expose] = expose,
  [FocusIn] = focusin,
  [KeyPress] = keypress,
  [MappingNotify] = mappingnotify,
  [MapRequest] = maprequest,
  [MotionNotify] = motionnotify,
  [PropertyNotify] = propertynotify,
  [ResizeRequest] = resizerequest,
  [UnmapNotify] = unmapnotify
};
static Atom wmatom[WMLast], netatom[NetLast], xatom[XLast];
static Bool running = True;
static Cur *cursor[CurLast];
static Clr **scheme;
static Display *dpy;
static Drw *drw;
static Monitor *mons = NULL, *selmon = NULL;

static Window root, wmcheckwin;

static xcb_connection_t *xcon;

/* configuration, allows nested code to access above variables */
#include "config.h"

struct Pertag {
  unsigned int curtag, prevtag; /* current and previous tag */
  int nmasters[LENGTH(tags) + 1]; /* number of windows in master area */
  Area areas[LENGTH(tags) + 1][3]; /* tiling areas */
  unsigned int sellts[LENGTH(tags) + 1]; /* selected layouts */
  const Layout *ltidxs[LENGTH(tags) + 1][2]; /* matrix of tags and layouts indexes  */
  Bool showbars[LENGTH(tags) + 1]; /* display bar for the current tag */
};

/* compile-time check if all tags fit into an unsigned int bit array. */
struct NumTags { char limitexceeded[LENGTH(tags) > 31 ? -1 : 1]; };

/* function implementations */
void
applyrules(Client *c) {
  const char *class, *instance;
  unsigned int i;
  const Rule *r;
  Monitor *m;
  XClassHint ch = { NULL, NULL };

  /* rule matching */
  c->isfloating = c->tags = 0;
  c->noswallow = -1;

  XGetClassHint(dpy, c->win, &ch);
  class    = ch.res_class ? ch.res_class : broken;
  instance = ch.res_name  ? ch.res_name  : broken;

  for (i = 0; i < LENGTH(rules); i++) {
    r = &rules[i];
    if ((!r->title || strstr(c->name, r->title))
    && (!r->class || strstr(class, r->class))
    && (!r->instance || strstr(instance, r->instance)))
    {
      c->isterminal = r->isterminal;
      c->noswallow = r->noswallow;
      c->isfloating = r->isfloating;
      c->tags |= r->tags;
      for (m = mons; m && m->num != r->monitor; m = m->next);
      if (m)
        c->mon = m;
    }
  }
  if (ch.res_class)
    XFree(ch.res_class);
  if (ch.res_name)
    XFree(ch.res_name);
  c->tags = c->tags & TAGMASK ? c->tags & TAGMASK : c->mon->tagset[c->mon->seltags];
}

int
applysizehints(Client *c, int *x, int *y, int *w, int *h, Bool interact) {
  Bool baseismin;
  Monitor *m = c->mon;

  /* set minimum possible */
  *w = MAX(1, *w);
  *h = MAX(1, *h);
  if (interact) {
    if (*x > sw)
      *x = sw - WIDTH(c);
    if (*y > sh)
      *y = sh - HEIGHT(c);
    if (*x + *w + 2 * c->bw < 0)
      *x = 0;
    if (*y + *h + 2 * c->bw < 0)
      *y = 0;
  } else {
    if (*x >= m->wx + m->ww)
      *x = m->wx + m->ww - WIDTH(c);
    if (*y >= m->wy + m->wh)
      *y = m->wy + m->wh - HEIGHT(c);
    if (*x + *w + 2 * c->bw <= m->wx)
      *x = m->wx;
    if (*y + *h + 2 * c->bw <= m->wy)
      *y = m->wy;
  }
  if (*h < bh)
    *h = bh;
  if (*w < bh)
    *w = bh;
  if (resizehints || c->isfloating || !c->mon->lt[c->mon->sellt]->arrange) {
    /* see last two sentences in ICCCM 4.1.2.3 */
    baseismin = c->basew == c->minw && c->baseh == c->minh;
    if (!baseismin) { /* temporarily remove base dimensions */
      *w -= c->basew;
      *h -= c->baseh;
    }
    /* adjust for aspect limits */
    if (c->mina > 0 && c->maxa > 0) {
      if (c->maxa < (float)*w / *h)
        *w = *h * c->maxa + 0.5;
      else if (c->mina < (float)*h / *w)
        *h = *w * c->mina + 0.5;
    }
    if (baseismin) { /* increment calculation requires this */
      *w -= c->basew;
      *h -= c->baseh;
    }
    /* adjust for increment value */
    if (c->incw)
      *w -= *w % c->incw;
    if (c->inch)
      *h -= *h % c->inch;

    /* restore base dimensions */
    *w = MAX(*w + c->basew, c->minw);
    *h = MAX(*h + c->baseh, c->minh);
    if (c->maxw)
      *w = MIN(*w, c->maxw);
    if (c->maxh)
      *h = MIN(*h, c->maxh);
  }
  return *x != c->x || *y != c->y || *w != c->w || *h != c->h;
}

void
arrange(Monitor *m) {
  if (m)
    showhide(m->stack);
  else for (m = mons; m; m = m->next)
    showhide(m->stack);
  if (m) {
    arrangemon(m);
    restack(m);
  } else for (m = mons; m; m = m->next)
    arrangemon(m);
}

void
arrangemon(Monitor *m) {
  strncpy(m->ltsymbol, m->lt[m->sellt]->symbol, sizeof m->ltsymbol);
  if (m->lt[m->sellt]->arrange)
    m->lt[m->sellt]->arrange(m);
  // XXX: restack calls required?
  // restack (m);
}

void
attach(Client *c) {
  c->next = c->mon->clients;
  c->mon->clients = c;
}

void
attachstack(Client *c) {
  c->snext = c->mon->stack;
  c->mon->stack = c;
}

void
swallow(Client *p, Client *c) {
  Client *s;

  if (c->noswallow > 0 || c->isterminal)
    return;
  if (c->noswallow < 0 && !swallowfloating && c->isfloating)
    return;

  detach(c);
  detachstack(c);

  setclientstate(c, WithdrawnState);
  XUnmapWindow(dpy, p->win);

  p->swallowing = c;
  c->mon = p->mon;

  Window w = p->win;
  p->win = c->win;
  c->win = w;

  XChangeProperty(dpy, c->win, netatom[NetClientList], XA_WINDOW, 32, PropModeReplace,
      (unsigned char*) &(p->win), 1);

  updatetitle(p);
  s = scanner ? c : p;
  XMoveResizeWindow(dpy, p->win, s->x, s->y, s->w, s->h);

  arrange(p->mon);
  configure(p);
  updateclientlist();
}

void
unswallow(Client *c) {
  c->win = c->swallowing->win;
  free(c->swallowing);
  c->swallowing = NULL;

  XDeleteProperty(dpy, c->win, netatom[NetClientList]);

  /* unfullscreen */
  setfullscreen(c, 0);
  updatetitle(c);
  arrange(c->mon);
  XMapWindow(dpy, c->win);
  XMoveResizeWindow(dpy, c->win, c->x, c->y, c->w, c->h);
  setclientstate(c, NormalState);
  focus(NULL);
  arrange(c->mon);
}


void
buttonpress(XEvent *e) {
  unsigned int i, x, click;
  Arg arg = {0};
  Client *c;
  Monitor *m;
  XButtonPressedEvent *ev = &e->xbutton;

  click = ClkRootWin;
  /* focus monitor if necessary */
  if ((m = wintomon(ev->window)) && m != selmon) {
    unfocus(selmon->sel, True);
    changemon(m);
    focus(NULL);
  }
  if (ev->window == selmon->barwin) {
    i = x = 0;
    do
      x += TEXTW(tags[i]);
    while (ev->x >= x && ++i < LENGTH(tags));
    if (i < LENGTH(tags)) {
      click = ClkTagBar;
      arg.ui = 1 << i;
    } else if (ev->x < x + blw)
      click = ClkLtSymbol;
    else if (ev->x > selmon->ww - TEXTW(stext) - getsystraywidth())
      click = ClkStatusText;
    else
      click = ClkWinTitle;
  } else if ((c = wintoclient(ev->window))) {
    focus(c);
    restack(selmon);
    XAllowEvents(dpy, ReplayPointer, CurrentTime);
    click = ClkClientWin;
  }
  for (i = 0; i < LENGTH(buttons); i++)
    if (click == buttons[i].click && buttons[i].func && buttons[i].button == ev->button
    && CLEANMASK(buttons[i].mask) == CLEANMASK(ev->state))
      buttons[i].func(click == ClkTagBar && buttons[i].arg.i == 0 ? &arg : &buttons[i].arg);
}

void
changemon(Monitor *m) {
  selmon = m;
  resizebarwin(selmon);
  updatesystray();
}

void
checkotherwm(void) {
  xerrorxlib = XSetErrorHandler(xerrorstart);
  /* this causes an error if some other window manager is running */
  XSelectInput(dpy, DefaultRootWindow(dpy), SubstructureRedirectMask);
  XSync(dpy, False);
  XSetErrorHandler(xerror);
  XSync(dpy, False);
}

void
cleanup(void) {
  Arg a = {.ui = ~0};
  Layout foo = { "", NULL };
  Monitor *m;
  size_t i;

  view(&a);
  selmon->lt[selmon->sellt] = &foo;
  for (m = mons; m; m = m->next)
    while (m->stack)
      unmanage(m->stack, False);
  XUngrabKey(dpy, AnyKey, AnyModifier, root);
  while (mons)
    cleanupmon(mons);
  for (i = 0; i < CurLast; i++)
    drw_cur_free(drw, cursor[i]);
  for (i = 0; i < LENGTH(colors); i++)
    free(scheme[i]);
  XDestroyWindow(dpy, wmcheckwin);
  drw_free(drw);
  if (showsystray) {
    XUnmapWindow(dpy, systray->win);
    XDestroyWindow(dpy, systray->win);
    free(systray);
  }
  XSync(dpy, False);
  XSetInputFocus(dpy, PointerRoot, RevertToPointerRoot, CurrentTime);
  XDeleteProperty(dpy, root, netatom[NetActiveWindow]);
}

void
cleanupmon(Monitor *mon) {
  Monitor *m;

  if (mon == mons)
    mons = mons->next;
  else {
    for (m = mons; m && m->next != mon; m = m->next);
    m->next = mon->next;
  }
  XUnmapWindow(dpy, mon->barwin);
  XDestroyWindow(dpy, mon->barwin);
  free(mon);
}

void
clientmessage(XEvent *e) {
  int i;
  XWindowAttributes wa;
  XSetWindowAttributes swa;
  XClientMessageEvent *cme = &e->xclient;
  Client *c = wintoclient(cme->window);

  if (showsystray && cme->window == systray->win && cme->message_type == netatom[NetSystemTrayOP]) {
    /* add systray icons */
    if (cme->data.l[1] == SYSTEM_TRAY_REQUEST_DOCK) {
      c = ecalloc(1, sizeof(Client));
      if (!(c->win = cme->data.l[2])) {
        free(c);
        return;
      }
      c->mon = selmon;
      c->next = systray->icons;
      systray->icons = c;
      if (!XGetWindowAttributes(dpy, c->win, &wa)) {
        /* use sane default */
        wa.width = bh;
        wa.height = bh;
        wa.border_width = 0;
      }
      c->x = c->oldx = c->y = c->oldy = 0;
      c->w = c->oldw = wa.width;
      c->h = c->oldh = wa.height;
      c->oldbw = wa.border_width;
      c->bw = 0;
      c->isfloating = True;
      /* reuse tags field as mapped status */
      c->tags = 1;
      updatesizehints(c);
      updatesystrayicongeom(c, wa.width, wa.height);
      XAddToSaveSet(dpy, c->win);
      XSelectInput(dpy, c->win, StructureNotifyMask | PropertyChangeMask | ResizeRedirectMask);
      XReparentWindow(dpy, c->win, systray->win, 0, 0);
      /* use parents background pixmap */
      swa.background_pixmap = ParentRelative;
      swa.background_pixel  = scheme[SchemeNorm][ColBg].pixel;
      XChangeWindowAttributes(dpy, c->win, CWBackPixmap|CWBackPixel, &swa);
      sendevent(c->win, netatom[Xembed], StructureNotifyMask, CurrentTime, XEMBED_EMBEDDED_NOTIFY, 0 , systray->win, XEMBED_EMBEDDED_VERSION);
      /* FIXME not sure if I have to send these events, too */
      sendevent(c->win, netatom[Xembed], StructureNotifyMask, CurrentTime, XEMBED_FOCUS_IN, 0 , systray->win, XEMBED_EMBEDDED_VERSION);
      sendevent(c->win, netatom[Xembed], StructureNotifyMask, CurrentTime, XEMBED_WINDOW_ACTIVATE, 0 , systray->win, XEMBED_EMBEDDED_VERSION);
      sendevent(c->win, netatom[Xembed], StructureNotifyMask, CurrentTime, XEMBED_MODALITY_ON, 0 , systray->win, XEMBED_EMBEDDED_VERSION);
      XSync(dpy, False);
      resizebarwin(selmon);
      updatesystray();
      setclientstate(c, NormalState);
    }
    return;
  }
  if (!c)
    return;
  if (cme->message_type == netatom[NetWMState]) {
    if (cme->data.l[1] == netatom[NetWMFullscreen]
    || cme->data.l[2] == netatom[NetWMFullscreen])
      setfullscreen(c, (cme->data.l[0] == 1 /* _NET_WM_STATE_ADD    */
        || (cme->data.l[0] == 2 /* _NET_WM_STATE_TOGGLE */ && !c->isfullscreen)));
  } else if (cme->message_type == netatom[NetActiveWindow]) {
    if (c != selmon->sel && !c->isurgent)
      seturgent(c, 1);

    if (!ISVISIBLE(c)) {
      for (i=0; !(c->tags & 1 << i); i++);
      view(&(Arg) {.ui = 1 << i});
    }
    pop(c);
  }
}

void
configure(Client *c) {
  XConfigureEvent ce;

  ce.type = ConfigureNotify;
  ce.display = dpy;
  ce.event = c->win;
  ce.window = c->win;
  ce.x = c->x;
  ce.y = c->y;
  ce.width = c->w;
  ce.height = c->h;
  ce.border_width = c->bw;
  ce.above = None;
  ce.override_redirect = False;
  XSendEvent(dpy, c->win, False, StructureNotifyMask, (XEvent *)&ce);
}

void
configurenotify(XEvent *e) {
  Client *c;
  Monitor *m;
  XConfigureEvent *ev = &e->xconfigure;
  Bool dirty;

  /* TODO: updategeom handling sucks, needs to be simplified */
  if (ev->window == root) {
    dirty = (sw != ev->width || sh != ev->height);
    sw = ev->width;
    sh = ev->height;
    if (updategeom() || dirty) {
      drw_resize(drw, sw, bh);
      updatebars();
      for (m = mons; m; m = m->next) {
        resizebarwin(m);
        for (c = m->clients; c; c = c->next)
          if (c->isfullscreen)
            resizeclient(c, m->mx, m->my, m->mw, m->mh);
        resizebarwin(m);
      }
      focus(NULL);
      arrange(NULL);
    }
  }
}

void
configurerequest(XEvent *e) {
  Client *c;
  Monitor *m;
  XConfigureRequestEvent *ev = &e->xconfigurerequest;
  XWindowChanges wc;

  if ((c = wintoclient(ev->window))) {
    if (ev->value_mask & CWBorderWidth)
      c->bw = ev->border_width;
    else if (c->isfloating || !selmon->lt[selmon->sellt]->arrange) {
      m = c->mon;
      if (ev->value_mask & CWX) {
        c->oldx = c->x;
        c->x = m->mx + ev->x;
      }
      if (ev->value_mask & CWY) {
        c->oldy = c->y;
        c->y = m->my + ev->y;
      }
      if (ev->value_mask & CWWidth) {
        c->oldw = c->w;
        c->w = ev->width;
      }
      if (ev->value_mask & CWHeight) {
        c->oldh = c->h;
        c->h = ev->height;
      }
      if ((c->x + c->w) > m->mx + m->mw && c->isfloating)
        c->x = m->mx + (m->mw / 2 - WIDTH(c) / 2); /* center in x direction */
      if ((c->y + c->h) > m->my + m->mh && c->isfloating)
        c->y = m->my + (m->mh / 2 - HEIGHT(c) / 2); /* center in y direction */
      if ((ev->value_mask & (CWX|CWY)) && !(ev->value_mask & (CWWidth|CWHeight)))
        configure(c);
      if (ISVISIBLE(c))
        XMoveResizeWindow(dpy, c->win, c->x, c->y, c->w, c->h);
    } else
      configure(c);
  } else {
    wc.x = ev->x;
    wc.y = ev->y;
    wc.width = ev->width;
    wc.height = ev->height;
    wc.border_width = ev->border_width;
    wc.sibling = ev->above;
    wc.stack_mode = ev->detail;
    XConfigureWindow(dpy, ev->window, ev->value_mask, &wc);
  }
  XSync(dpy, False);
}

Monitor *
createmon(void) {
  Monitor *m;
  int i, j;

  m = ecalloc(1, sizeof(Monitor));
  m->tagset[0] = m->tagset[1] = 1;
  m->nmaster = nmaster;
  m->showbar = showbar;
  m->topbar = topbar;
  m->lt[0] = &layouts[default_layout];
  m->lt[1] = &layouts[1 % LENGTH(layouts)];
  strncpy(m->ltsymbol, layouts[0].symbol, sizeof m->ltsymbol);

  m->pertag = ecalloc(1, sizeof(Pertag));
  m->pertag->curtag = m->pertag->prevtag = 1;
  for (i=0; i <= LENGTH(tags); i++) {
    /* init nmaster */
    m->pertag->nmasters[i] = m->nmaster;

    /* init tiling dirs and facts */
    for (j = 0; j < 3; j++) {
      m->pertag->areas[i][j].dir = MIN(dirs[j], ((int[]){ 3, 1, 1 }[j]));
      m->pertag->areas[i][j].fact = TRUNC(facts[j], 0.1, 10);
    }

    /* init layouts */
    m->pertag->ltidxs[i][0] = m->lt[0];
    m->pertag->ltidxs[i][1] = m->lt[1];
    m->pertag->sellts[i] = m->sellt;

    /* init showbar */
    m->pertag->showbars[i] = m->showbar;
  }
  return m;
}

void
destroynotify(XEvent *e) {
  Client *c;
  XDestroyWindowEvent *ev = &e->xdestroywindow;

  if ((c = wintoclient(ev->window))
      || (c = swallowingclient(ev->window)))
    unmanage(c, True);

  else if ((c = wintosystrayicon(ev->window))) {
    removesystrayicon(c);
    resizebarwin(selmon);
    updatesystray();
  }
}

void
detach(Client *c) {
  Client **tc;

  for (tc = &c->mon->clients; *tc && *tc != c; tc = &(*tc)->next);
  *tc = c->next;
}

void
detachstack(Client *c) {
  Client **tc, *t;

  for (tc = &c->mon->stack; *tc && *tc != c; tc = &(*tc)->snext);
  *tc = c->snext;

  if (c == c->mon->sel) {
    for (t = c->mon->stack; t && !ISVISIBLE(t); t = t->snext);
    c->mon->sel = t;
  }
}

Monitor *
dirtomon(int dir) {
  Monitor *m = NULL;

  if (dir > 0) {
    if (!(m = selmon->next))
      m = mons;
  } else if (selmon == mons)
    for (m = mons; m->next; m = m->next);
  else
    for (m = mons; m->next != selmon; m = m->next);
  return m;
}

void
drawbar(Monitor *m) {
  int x, w, tw = 0, stw = 0;
  int boxs = drw->fonts->h / 9;
  int boxw = drw->fonts->h / 6 + 2;
  unsigned int i, occ = 0, urg = 0;
  Client *c;

  if (showsystray && m == selmon)
    stw = getsystraywidth();

  /* draw status first so it can be overdrawn by tags later */
  if (m == selmon) { /* status is only drawn on selected monitor */
    drw_setscheme(drw, scheme[SchemeNorm]);

    tw = TEXTW(stext) - lrpad / 2 + 2; /* 2px right padding */
    drw_text(drw, m->ww - tw - stw, 0, tw, bh, lrpad / 2 - 2, stext, 0);
  }

  resizebarwin (m);
  for (c = m->clients; c; c = c->next) {
    occ |= c->tags;
    if (c->isurgent)
      urg |= c->tags;
  }

  x = 0;
  for (i = 0; i < LENGTH(tags); i++) {
    w = TEXTW(tags[i]);
    drw_setscheme(drw, scheme[m->tagset[m->seltags] & 1 << i ? SchemeSel : SchemeNorm]);
    drw_text(drw, x, 0, w, bh, lrpad / 2, tags[i], urg & 1 << i);
    if (occ & 1 << i)
      drw_rect(drw, x + boxs, boxs, boxw, boxw,
        m == selmon && selmon->sel && selmon->sel->tags & 1 << i,
        urg & 1 << i);
    x += w;
  }
  w = blw = TEXTW(m->ltsymbol);
  drw_setscheme(drw, scheme[SchemeNorm]);
  x = drw_text(drw, x, 0, w, bh, lrpad / 2, m->ltsymbol, 0);

  if ((w = m->ww - tw - stw - x) > bh) {
    if (m->sel) {
      drw_setscheme(drw, scheme[m == selmon ? SchemeSel : SchemeNorm]);
      drw_text(drw, x, 0, w, bh, lrpad / 2, m->sel->name, 0);
      if (m->sel->isfloating)
        drw_rect(drw, x + boxs, boxs, boxw, boxw, m->sel->isfixed, 0);
    } else {
      drw_setscheme(drw, scheme[SchemeNorm]);
      drw_rect(drw, x, 0, w, bh, 1, 1);
    }
  }
  drw_map(drw, m->barwin, 0, 0, m->ww - stw, bh);
}

void
drawbars(void) {
  Monitor *m;

  for (m = mons; m; m = m->next)
    drawbar(m);
  updatesystray();
}

void
enternotify(XEvent *e) {
  Client *c;
  Monitor *m;
  XCrossingEvent *ev = &e->xcrossing;

  if ((ev->mode != NotifyNormal || ev->detail == NotifyInferior) && ev->window != root)
    return;
  c = wintoclient(ev->window);
  m = c ? c->mon : wintomon(ev->window);
  if (m != selmon) {
    unfocus(selmon->sel, True);
    changemon(m);
  } else if (!c || c == selmon->sel)
    return;
  focus(c);
}

void
expose(XEvent *e) {
  Monitor *m;
  XExposeEvent *ev = &e->xexpose;

  if (ev->count == 0 && (m = wintomon(ev->window))) {
    drawbar(m);
    if (m == selmon)
      updatesystray();
  }
}

void
focus(Client *c) {
  if (!c || !ISVISIBLE(c))
    for (c = selmon->stack; c && !ISVISIBLE(c); c = c->snext);
  /* was if (selmon->sel) */
  if (selmon->sel && selmon->sel != c)
    unfocus(selmon->sel, False);
  if (c) {
    if (c->mon != selmon)
      changemon(c->mon);
    if (c->isurgent)
      seturgent(c, False);
    detachstack(c);
    attachstack(c);
    grabbuttons(c, True);
    XSetWindowBorder(dpy, c->win, scheme[SchemeSel][ColBorder].pixel);
    setfocus(c);
  } else {
    XSetInputFocus(dpy, root, RevertToPointerRoot, CurrentTime);
    XDeleteProperty(dpy, root, netatom[NetActiveWindow]);
  }
  selmon->sel = c;
  drawbars();
}

/* there are some broken focus acquiring clients needing extra handling */
void
focusin(XEvent *e) {
  XFocusChangeEvent *ev = &e->xfocus;

  if (selmon->sel && ev->window != selmon->sel->win)
    setfocus(selmon->sel);
}

void
focusmon(const Arg *arg) {
  Monitor *m;

  if (!mons->next)
    return;
  if ((m = dirtomon(arg->i)) == selmon)
    return;
  unfocus(selmon->sel, True);
  changemon(m);
  focus(NULL);
}

void
focusstack(const Arg *arg) {
  Client *c = NULL, *i;

  if (!selmon->sel)
    return;
  if (arg->i > 0) {
    for (c = selmon->sel->next; c && !ISVISIBLE(c); c = c->next);
    if (!c)
      for (c = selmon->clients; c && !ISVISIBLE(c); c = c->next);
  } else {
    for (i = selmon->clients; i != selmon->sel; i = i->next)
      if (ISVISIBLE(i))
        c = i;
    if (!c)
      for (; i; i = i->next)
        if (ISVISIBLE(i))
          c = i;
  }
  if (c) {
    focus(c);
    restack(selmon);
    XRaiseWindow(dpy, c->win);
  }
}

Atom
getatomprop(Client *c, Atom prop) {
  int di;
  unsigned long dl;
  unsigned char *p = NULL;
  Atom da, atom = None;

  /* FIXME getatomprop should return the number of items and a pointer to
   * the stored data instead of this workaround */
  Atom req = XA_ATOM;
  if (prop == xatom[XembedInfo])
    req = xatom[XembedInfo];

  if (XGetWindowProperty(dpy, c->win, prop, 0L, sizeof atom, False, req,
        &da, &di, &dl, &dl, &p) == Success && p) {
    atom = *(Atom *)p;
    if (da == xatom[XembedInfo] && dl == 2)
      atom = ((Atom *)p)[1];
    XFree(p);
  }
  return atom;
}

Bool
getrootptr(int *x, int *y) {
  int di;
  unsigned int dui;
  Window dummy;

  return XQueryPointer(dpy, root, &dummy, &dummy, x, y, &di, &di, &dui);
}

long
getstate(Window w) {
  int format;
  long result = -1;
  unsigned char *p = NULL;
  unsigned long n, extra;
  Atom real;

  if (XGetWindowProperty(dpy, w, wmatom[WMState], 0L, 2L, False, wmatom[WMState],
    &real, &format, &n, &extra, (unsigned char **)&p) != Success)
    return -1;
  if (n != 0)
    result = *p;
  XFree(p);
  return result;
}

unsigned int
getsystraywidth() {
  unsigned int w = 0;
  Client *i;
  if (showsystray)
    for (i = systray->icons; i; w += i->w + systrayspacing, i = i->next) ;
  return w ? w + systrayspacing : 1;
}

Bool
gettextprop(Window w, Atom atom, char *text, unsigned int size) {
  char **list = NULL;
  int n;
  XTextProperty name;

  if (!text || size == 0)
    return False;
  text[0] = '\0';
  XGetTextProperty(dpy, w, &name, atom);
  if (!name.nitems)
    return False;
  if (name.encoding == XA_STRING)
    strncpy(text, (char *)name.value, size - 1);
  else {
    if (XmbTextPropertyToTextList(dpy, &name, &list, &n) >= Success && n > 0 && *list) {
      strncpy(text, *list, size - 1);
      XFreeStringList(list);
    }
  }
  text[size - 1] = '\0';
  XFree(name.value);
  return True;
}

void
grabbuttons(Client *c, Bool focused) {
  updatenumlockmask();
  {
    unsigned int i, j;
    unsigned int modifiers[] = { 0, LockMask, numlockmask, numlockmask|LockMask };
    XUngrabButton(dpy, AnyButton, AnyModifier, c->win);
    if (!focused)
      XGrabButton(dpy, AnyButton, AnyModifier, c->win, False,
        BUTTONMASK, GrabModeSync, GrabModeSync, None, None);
    for (i = 0; i < LENGTH(buttons); i++)
      if (buttons[i].click == ClkClientWin)
        for (j = 0; j < LENGTH(modifiers); j++)
          XGrabButton(dpy, buttons[i].button,
            buttons[i].mask | modifiers[j],
            c->win, False, BUTTONMASK,
            GrabModeAsync, GrabModeSync, None, None);
  }
}

void
grabkeys(void) {
  updatenumlockmask();
  {
    unsigned int i, j;
    unsigned int modifiers[] = { 0, LockMask, numlockmask, numlockmask|LockMask };
    KeyCode code;

    XUngrabKey(dpy, AnyKey, AnyModifier, root);
    for (i = 0; i < LENGTH(keys); i++)
      if ((code = XKeysymToKeycode(dpy, keys[i].keysym)))
        for (j = 0; j < LENGTH(modifiers); j++)
          XGrabKey(dpy, code, keys[i].mod | modifiers[j], root,
            True, GrabModeAsync, GrabModeAsync);
  }
}

void
grid(Monitor *m)
{
  unsigned int i, n, cx, cy, cw, ch, aw, ah, ncols, nrows;
  XWindowAttributes wa;
  Client *c;

  // Add grid direction.
  Area *ma = m->pertag->areas[m->pertag->curtag] + 1;
  m->ltsymbol[1] = (char[]){ '|', '-' }[ma->dir];

  for (n = 0, c = m->clients; c; c = c->next)
    if (ISVISIBLE(c) && !c->isfloating)
      n++;
  if (n) {
    if (ma->dir == DirHor) {
      nrows = (int)ceilf(sqrtf(n));
      ncols = (int)ceilf(((float)n / nrows));
    } else {
      ncols = (int)ceilf(sqrtf(n));
      nrows = (int)ceilf(((float)n / ncols));
    }

    // Windows dimension.
    cw = m->ww / (ncols ? ncols : 1);
    ch = m->wh;
    if (!selmon->showbar) {
      if (!XGetWindowAttributes(dpy, selmon->barwin, &wa))
        return ;
      ch += wa.height;
    }
    ch /= (nrows ? nrows : 1);
    for (i = 0, c = nexttiled(m->clients); c; c = nexttiled(c->next)) {
      cx = m->wx + (i / nrows) * cw;
      cy = m->wy + (i % nrows) * ch;
      ah = ((i + 1) % nrows == 0) ? m->wh - ch * nrows : 0;
      aw = (i >= nrows * (ncols - 1)) ? m->ww - cw * ncols : 0;
      resize(c,cx,cy,cw - 2 * c->bw + aw, ch - 2 * c->bw +ah, False);
      i++;
    }
  }
}

void
incnmaster(const Arg *arg) {
  selmon->nmaster = selmon->pertag->nmasters[selmon->pertag->curtag] = MAX(selmon->nmaster + arg->i, 0);
  arrange(selmon);
}

#ifdef XINERAMA
static Bool
isuniquegeom(XineramaScreenInfo *unique, size_t n, XineramaScreenInfo *info) {
  while (n--)
    if (unique[n].x_org == info->x_org && unique[n].y_org == info->y_org
    && unique[n].width == info->width && unique[n].height == info->height)
      return False;
  return True;
}
#endif /* XINERAMA */

void
keypress(XEvent *e) {
  unsigned int i;
  int keysyms_per_keycode;
  KeySym *keysym;
  XKeyEvent *ev;

  ev = &e->xkey;
  keysym = XGetKeyboardMapping(dpy, (KeyCode)ev->keycode, 1, &keysyms_per_keycode);
  for (i = 0; i < LENGTH(keys); i++)
    if (*keysym == keys[i].keysym
    && CLEANMASK(keys[i].mod) == CLEANMASK(ev->state)
    && keys[i].func)
      keys[i].func(&(keys[i].arg));
  XFree(keysym);
}

void
killclient(const Arg *arg) {
  if (!selmon->sel)
    return;
  if(!sendevent(selmon->sel->win, wmatom[WMDelete], NoEventMask, wmatom[WMDelete], CurrentTime, 0, 0, 0)) {
    XGrabServer(dpy);
    XSetErrorHandler(xerrordummy);
    XSetCloseDownMode(dpy, DestroyAll);
    XKillClient(dpy, selmon->sel->win);
    XSync(dpy, False);
    XSetErrorHandler(xerror);
    XUngrabServer(dpy);
  }
}

void
manage(Window w, XWindowAttributes *wa) {
  Client *c, *t = NULL, *term = NULL;
  Window trans = None;
  XWindowChanges wc;

  c = ecalloc(1, sizeof(Client));
  c->win = w;
  c->pid = winpid(w);
  updatetitle(c);
  if (XGetTransientForHint(dpy, w, &trans) && (t = wintoclient(trans))) {
    c->mon = t->mon;
    c->tags = t->tags;
  }
  else {
    c->mon = selmon;
    applyrules(c);
    term = termforwin(c);
  }
  /* geometry */
  c->x = c->oldx = wa->x;
  c->y = c->oldy = wa->y;
  c->w = c->oldw = wa->width;
  c->h = c->oldh = wa->height;
  c->oldbw = wa->border_width;

  if (c->x + WIDTH(c) > c->mon->mx + c->mon->mw)
    c->x = c->mon->mx + c->mon->mw - WIDTH(c);
  if (c->y + HEIGHT(c) > c->mon->my + c->mon->mh)
    c->y = c->mon->my + c->mon->mh - HEIGHT(c);
  c->x = MAX(c->x, c->mon->mx);
  /* only fix client y-offset, if the client center might cover the bar */
  c->y = MAX(c->y, ((c->mon->by == c->mon->my) && (c->x + (c->w / 2) >= c->mon->wx)
    && (c->x + (c->w / 2) < c->mon->wx + c->mon->ww)) ? bh : c->mon->my);
  c->bw = borderpx;

  wc.border_width = c->bw;
  XConfigureWindow(dpy, w, CWBorderWidth, &wc);
  XSetWindowBorder(dpy, w, scheme[SchemeNorm][ColBorder].pixel);
  configure(c); /* propagates border_width, if size doesn't change */
  updatewindowtype(c);
  updatesizehints(c);
  updatewmhints(c);
  XSelectInput(dpy, w, EnterWindowMask|FocusChangeMask|PropertyChangeMask|StructureNotifyMask);
  grabbuttons(c, False);
  if (!c->isfloating)
    c->isfloating = c->oldstate = trans != None || c->isfixed;
  if (c->isfloating)
    XRaiseWindow(dpy, c->win);
  attach(c);
  attachstack(c);
  XChangeProperty(dpy, root, netatom[NetClientList], XA_WINDOW, 32, PropModeAppend,
    (unsigned char *) &(c->win), 1);
  XMoveResizeWindow(dpy, c->win, c->x + 2 * sw, c->y, c->w, c->h); /* some windows require this */
  setclientstate(c, NormalState);
  if (c->mon == selmon)
    unfocus(selmon->sel, False);
  c->mon->sel = c;
  arrange(c->mon);
  XMapWindow(dpy, c->win);
  // XXX: XRaiseWindow required?
  XRaiseWindow(dpy, c->win);
  if (term)
    swallow(term, c);
  focus(NULL);
}

void
mappingnotify(XEvent *e) {
  XMappingEvent *ev = &e->xmapping;

  XRefreshKeyboardMapping(ev);
  if (ev->request == MappingKeyboard)
    grabkeys();
}

void
maprequest(XEvent *e) {
  static XWindowAttributes wa;
  XMapRequestEvent *ev = &e->xmaprequest;
  Client *i;
  if ((i = wintosystrayicon(ev->window))) {
    sendevent(i->win, netatom[Xembed], StructureNotifyMask, CurrentTime, XEMBED_WINDOW_ACTIVATE, 0, systray->win, XEMBED_EMBEDDED_VERSION);
    resizebarwin(selmon);
    updatesystray();
  }

  updateclientlist();

  if (!XGetWindowAttributes(dpy, ev->window, &wa))
    return;
  if (wa.override_redirect)
    return;
  if (!wintoclient(ev->window))
    manage(ev->window, &wa);
}

void
monocle(Monitor *m) {
  unsigned int n = 0;
  Client *c;

  for (c = m->clients; c; c = c->next)
    if (ISVISIBLE(c))
      n++;
  if (n > 0) /* override layout symbol */
    snprintf(m->ltsymbol, sizeof m->ltsymbol, "[%d]", n);
  for (c = nexttiled(m->clients); c; c = nexttiled(c->next))
    resize(c, m->wx + c->bw, m->wy, m->ww - 2 * c->bw, m->wh - 2 * c->bw, False);
}

void
motionnotify(XEvent *e) {
  static Monitor *mon = NULL;
  Monitor *m;
  XMotionEvent *ev = &e->xmotion;

  if (ev->window != root)
    return;
  if ((m = recttomon(ev->x_root, ev->y_root, 1, 1)) != mon && mon) {
    unfocus(selmon->sel, True);
    changemon(m);
    focus(NULL);
  }
  mon = m;
}

void
movemouse(const Arg *arg) {
  int x, y, ocx, ocy, nx, ny;
  Client *c;
  Monitor *m;
  XEvent ev;
  Time lasttime = 0;

  if (!(c = selmon->sel))
    return;
  if (c->isfullscreen) /* no support moving fullscreen windows by mouse */
    return;
  restack(selmon);
  ocx = c->x;
  ocy = c->y;
  if (XGrabPointer(dpy, root, False, MOUSEMASK, GrabModeAsync, GrabModeAsync,
    None, cursor[CurMove]->cursor, CurrentTime) != GrabSuccess)
    return;
  if (!getrootptr(&x, &y))
    return;
  do {
    XMaskEvent(dpy, MOUSEMASK|ExposureMask|SubstructureRedirectMask, &ev);
    switch(ev.type) {
      case ConfigureRequest:
      case Expose:
      case MapRequest:
        handler[ev.type](&ev);
        break;
      case MotionNotify:
        if ((ev.xmotion.time - lasttime) <= (1000 / 60))
          continue;
        lasttime = ev.xmotion.time;

        nx = ocx + (ev.xmotion.x - x);
        ny = ocy + (ev.xmotion.y - y);
        if (abs(selmon->wx - nx) < snap)
          nx = selmon->wx;
        else if (abs((selmon->wx + selmon->ww) - (nx + WIDTH(c))) < snap)
          nx = selmon->wx + selmon->ww - WIDTH(c);
        if (abs(selmon->wy - ny) < snap)
          ny = selmon->wy;
        else if (abs((selmon->wy + selmon->wh) - (ny + HEIGHT(c))) < snap)
          ny = selmon->wy + selmon->wh - HEIGHT(c);
        if (!c->isfloating && selmon->lt[selmon->sellt]->arrange
        && (abs(nx - c->x) > snap || abs(ny - c->y) > snap))
          togglefloating(NULL);
        if (!selmon->lt[selmon->sellt]->arrange || c->isfloating)
          resize(c, nx, ny, c->w, c->h, True);
        break;
    }
  } while (ev.type != ButtonRelease);
  XUngrabPointer(dpy, CurrentTime);
  if ((m = recttomon(c->x, c->y, c->w, c->h)) != selmon) {
    sendmon(c, m);
    changemon(m);
    focus(NULL);
  }
}
void
movestack(const Arg *arg) {
  Client *c = NULL, *p = NULL, *pc = NULL, *i;
  if (selmon->clients) {
    if (arg->i < 0) {
      // Find client before selmon->sel
      for (i = selmon->clients; i != selmon->sel; i = i->next) {
        if (ISVISIBLE(i) && !i->isfloating) {
          c = i;
        }
      }
      if (!c) {
        for (; i; i = i->next) {
          if (ISVISIBLE(i) && !i->isfloating) {
            c = i;
          }
        }
      }
    } else {
      // Find client after selmon->sel
      for (c = selmon->sel->next; c && (!ISVISIBLE(c) || c->isfloating); c = c->next);
      if (!c) {
        for (c = selmon->clients; c != selmon->sel && (!ISVISIBLE(c) || c->isfloating); c = c->next);
      }
    }

    for (i = selmon->clients; i && (!p || !pc); i = i->next) {
      if (i->next == selmon->sel) {
        p = i;
      } else if (i->next == c) {
        pc = i;
      }
    }

    if (c && c != selmon->sel) {
      Client *tmp = selmon->sel->next == c ? selmon->sel: selmon->sel->next;
      selmon->sel->next = c->next == selmon->sel ? c : c->next;
      c->next = tmp;

      if (p && p != c) {
        p->next = c;
      }
      if (pc && pc != selmon->sel) {
        pc->next = selmon->sel;
      }
      if (selmon->sel == selmon->clients) {
        selmon->clients = c;
      }
      else if (c == selmon->clients) {
        selmon->clients = selmon->sel;
      }
      arrange(selmon);
    }
  }
}

Client *
nexttiled(Client *c) {
  for (; c && (c->isfloating || !ISVISIBLE(c)); c = c->next);
  return c;
}

void
pop(Client *c) {
  detach(c);
  attach(c);
  focus(c);
  arrange(c->mon);
}

void
propertynotify(XEvent *e) {
  Client *c;
  Window trans;
  XPropertyEvent *ev = &e->xproperty;

  if ((c = wintosystrayicon(ev->window))) {
    if (ev->atom == XA_WM_NORMAL_HINTS) {
      updatesizehints(c);
      updatesystrayicongeom(c, c->w, c->h);
    }
    else
      updatesystrayiconstate(c, ev);
    resizebarwin(selmon);
    updatesystray();
  }
  if ((ev->window == root) && (ev->atom == XA_WM_NAME))
    updatestatus();
  else if (ev->state == PropertyDelete)
    return; /* ignore */
  else if ((c = wintoclient(ev->window))) {
    switch(ev->atom) {
      default: break;
      case XA_WM_TRANSIENT_FOR:
        if (!c->isfloating && (XGetTransientForHint(dpy, c->win, &trans)) &&
          (c->isfloating = (wintoclient(trans)) != NULL))
          arrange(c->mon);
        break;
      case XA_WM_NORMAL_HINTS:
        updatesizehints(c);
        break;
      case XA_WM_HINTS:
        updatewmhints(c);
        drawbars();
        break;
    }
    if (ev->atom == XA_WM_NAME || ev->atom == netatom[NetWMName]) {
      updatetitle(c);
      if (c == c->mon->sel)
        drawbar(c->mon);
    }
    if (ev->atom == netatom[NetWMWindowType])
      updatewindowtype(c);
  }
}

void
quit(const Arg *arg) {
  Monitor *m = mons;
  if (systray->icons)
    return;
  for (; m; m = m->next) {
    if (m->clients) {
      return;
    }
  }
  running = False;
}

Monitor *
recttomon(int x, int y, int w, int h) {
  Monitor *m, *r = selmon;
  int a, area = 0;

  for (m = mons; m; m = m->next)
    if ((a = INTERSECT(x, y, w, h, m)) > area) {
      area = a;
      r = m;
    }
  return r;
}

void
restart(const Arg *arg)
{
  static char *self[] = {"dwm", NULL};
  Monitor *m;
  Client *c;
  for (m = mons; m; m = m->next) {
    for (c = m->clients; c; c = c->next) {
      if (c->swallowing) {
        return;
      }
    }
  }
  execvp(self[0], self);
}

void
removesystrayicon(Client *i) {
  Client **ii;

  if (!showsystray || !i)
    return;
  for (ii = &systray->icons; *ii && *ii != i; ii = &(*ii)->next);
  if (ii)
    *ii = i->next;
  free(i);
}


void
resize(Client *c, int x, int y, int w, int h, Bool interact) {
  if (applysizehints(c, &x, &y, &w, &h, interact))
    resizeclient(c, x, y, w, h);
}

void
resizebarwin(Monitor *m) {
  unsigned int w = m->ww;
  if (showsystray && m == selmon)
    w -= getsystraywidth();
  XMoveResizeWindow(dpy, m->barwin, m->wx, m->by, w, bh);
}

void
resizeclient(Client *c, int x, int y, int w, int h) {
  XWindowChanges wc;

  c->oldx = c->x; c->x = wc.x = x;
  c->oldy = c->y; c->y = wc.y = y;
  c->oldw = c->w; c->w = wc.width = w;
  c->oldh = c->h; c->h = wc.height = h;
  wc.border_width = c->bw;
  XConfigureWindow(dpy, c->win, CWX|CWY|CWWidth|CWHeight|CWBorderWidth, &wc);
  configure(c);
  XSync(dpy, False);
}

void
resizemouse(const Arg *arg) {
  int ocx, ocy, ocw, och, xc, yc;
  int nx, ny, nw, nh;
  Client *c;
  Monitor *m;
  XEvent ev;
  Time lasttime = 0;
  Cursor cur;

  if (!(c = selmon->sel))
    return;
  if (c->isfullscreen) /* no support resizing fullscreen windows by mouse */
    return;
  restack(selmon);
  ocx = c->x;
  ocy = c->y;
  ocw = c->w + (c->bw << 1);
  och = c->h + (c->bw << 1);

  if (getrootptr(&xc, &yc) == False)
    return;

  xc = ((xc - ocx) << 1) < c->w + c->bw - 1;
  yc = ((yc - ocy) << 1) < c->h + c->bw - 1;
  cur = cursor[CurResizeBottomRight + xc + (yc << 1)]->cursor;

  if (XGrabPointer(dpy, root, False, MOUSEMASK, GrabModeAsync, GrabModeAsync,
    None, cur, CurrentTime) != GrabSuccess)
    return;

  XWarpPointer(dpy, None, c->win, 0, 0, 0, 0, xc ? -1 : ocw, yc ? -1 : c->h);

  do {
    XMaskEvent(dpy, MOUSEMASK|ExposureMask|SubstructureRedirectMask, &ev);
    switch(ev.type) {
      case ConfigureRequest:
      case Expose:
      case MapRequest:
        handler[ev.type](&ev);
        break;
      case MotionNotify:
        if ((ev.xmotion.time - lasttime) <= (1000 / 60))
          continue;
        lasttime = ev.xmotion.time;

        nx = xc ? ev.xmotion.x: ocx;
        ny = yc ? ev.xmotion.y: ocy;
        nw = MAX(xc ? (ocx + ocw) - nx: ev.xmotion.x - ocx, 1) - (c->bw << 1);
        nh = MAX(yc ? (ocy + och) - ny: ev.xmotion.y - ocy, 1) - (c->bw << 1);
        if (c->mon->wx + nw >= selmon->wx && c->mon->wx + nw <= selmon->wx + selmon->ww
            && c->mon->wy + nh >= selmon->wy && c->mon->wy + nh <= selmon->wy + selmon->wh)
        {
          if (!c->isfloating && selmon->lt[selmon->sellt]->arrange
              && (abs(nw - ocw) > snap || abs(nh - och) > snap))
            togglefloating(NULL);
        }

        if ((!selmon->lt[selmon->sellt]->arrange || c->isfloating)
            && ((!c->incw || nw % c->incw == 0) || (!c->inch || nh % c->inch == 0))) {
          resize(c, nx, ny, nw, nh, True);
        }
        break;
    }
  } while (ev.type != ButtonRelease);

  XUngrabPointer(dpy, CurrentTime);
  while (XCheckMaskEvent(dpy, EnterWindowMask, &ev));
  if ((m = recttomon(c->x, c->y, c->w, c->h)) != selmon) {
    sendmon(c, m);
    changemon(m);
    focus(NULL);
  }
}

void
resizerequest(XEvent *e) {
  XResizeRequestEvent *ev = &e->xresizerequest;
  Client *i;

  if ((i = wintosystrayicon(ev->window))) {
    updatesystrayicongeom(i, ev->width, ev->height);
    resizebarwin(selmon);
    updatesystray();
  }
}

void
restack(Monitor *m) {
  Client *c;
  XEvent ev;
  XWindowChanges wc;

  drawbar(m);
  if (!m->sel)
    return;
  if (m->sel->isfloating || !m->lt[m->sellt]->arrange)
    XRaiseWindow(dpy, m->sel->win);
  if (m->lt[m->sellt]->arrange) {
    wc.stack_mode = Below;
    wc.sibling = m->barwin;
    for (c = m->stack; c; c = c->snext)
      if (!c->isfloating && ISVISIBLE(c)) {
        XConfigureWindow(dpy, c->win, CWSibling|CWStackMode, &wc);
        wc.sibling = c->win;
      }
  }
  XSync(dpy, False);
  while (XCheckMaskEvent(dpy, EnterWindowMask, &ev));
}

void
run(void) {
  XEvent ev;
  /* main event loop */
  XSync(dpy, False);
  while (running && !XNextEvent(dpy, &ev)) {
    if (handler[ev.type]) {
      handler[ev.type](&ev); /* call handler */
    }
  }
}

void
scan(void) {
  unsigned int i, num;
  Window d1, d2, *wins = NULL;
  char swin[256];
  XWindowAttributes wa;

  scanner = 1;

  if (XQueryTree(dpy, root, &d1, &d2, &wins, &num)) {
    for (i = 0; i < num; i++) {
      if (!XGetWindowAttributes(dpy, wins[i], &wa)
          || wa.override_redirect || XGetTransientForHint(dpy, wins[i], &d1))
        continue;
      if (wa.map_state == IsViewable || getstate(wins[i]) == IconicState)
        manage(wins[i], &wa);
      else if (gettextprop(wins[i], netatom[NetClientList], swin, sizeof swin))
        manage(swin[i], &wa);
    }
    for (i = 0; i < num; i++) { /* now the transients */
      if (!XGetWindowAttributes(dpy, wins[i], &wa))
        continue;
      if (XGetTransientForHint(dpy, wins[i], &d1)
          && (wa.map_state == IsViewable || getstate(wins[i]) == IconicState))
        manage(wins[i], &wa);
    }
    if (wins)
      XFree(wins);
  }
  scanner = 0;
}

void
sendmon(Client *c, Monitor *m) {
  if (c->mon == m)
    return;
  unfocus(c, True);
  detach(c);
  detachstack(c);
  c->mon = m;
  c->tags = m->tagset[m->seltags]; /* assign tags of target monitor */
  attach(c);
  attachstack(c);
  focus(NULL);
  arrange(NULL);
}

void
setclientstate(Client *c, long state) {
  long data[] = { state, None };

  XChangeProperty(dpy, c->win, wmatom[WMState], wmatom[WMState], 32,
      PropModeReplace, (unsigned char *)data, 2);
}

void
setdirs(const Arg *arg) {
  int *dirs = (int *)arg->v, i, n;
  Area *areas = selmon->pertag->areas[selmon->pertag->curtag];

  for (i = 0; i < 3; i++) {
    n = (int[]){ 4, 2, 2 }[i];
    areas[i].dir = ISINC(dirs[i]) ?
      MOD((int)areas[i].dir + GETINC(dirs[i]), n) : TRUNC(dirs[i], 0, n - 1);
  }
  arrange(selmon);
}

void
setfacts(const Arg *arg) {
  float *facts = (float *)arg->v;
  Area *areas = selmon->pertag->areas[selmon->pertag->curtag];
  int i;

  for (i = 0; i < 3; i++)
    areas[i].fact = TRUNC(ISINC(facts[i]) ?
        areas[i].fact + GETINC(facts[i]) : facts[i], 0.1, 10);
  arrange(selmon);
}

Bool
sendevent(Window w, Atom proto, int mask, long d0, long d1, long d2, long d3, long d4) {
  int n;
  Atom *protocols, mt;
  Bool exists = False;
  XEvent ev;

  if(proto == wmatom[WMTakeFocus] || proto == wmatom[WMDelete]) {
    mt = wmatom[WMProtocols];
    if(XGetWMProtocols(dpy, w, &protocols, &n)) {
      while(!exists && n--)
        exists = protocols[n] == proto;
      XFree(protocols);
    }
  } else {
    exists = True;
    mt = proto;
  }
  if(exists) {
    ev.type = ClientMessage;
    ev.xclient.window = w;
    ev.xclient.message_type = mt;
    ev.xclient.format = 32;
    ev.xclient.data.l[0] = d0;
    ev.xclient.data.l[1] = d1;
    ev.xclient.data.l[2] = d2;
    ev.xclient.data.l[3] = d3;
    ev.xclient.data.l[4] = d4;
    XSendEvent(dpy, w, False, mask, &ev);
  }
  return exists;
}

void
setfocus(Client *c) {
  if (!c->neverfocus) {
    XSetInputFocus(dpy, c->win, RevertToPointerRoot, CurrentTime);
    XChangeProperty(dpy, root, netatom[NetActiveWindow],
        XA_WINDOW, 32, PropModeReplace,
        (unsigned char *) &(c->win), 1);
  }
  sendevent(c->win, wmatom[WMTakeFocus], NoEventMask, wmatom[WMTakeFocus], CurrentTime, 0, 0, 0);
}

void
setfullscreen(Client *c, Bool fullscreen) {
  if (fullscreen && !c->isfullscreen) {
    XChangeProperty(dpy, c->win, netatom[NetWMState], XA_ATOM, 32,
        PropModeReplace, (unsigned char*)&netatom[NetWMFullscreen], 1);
    c->isfullscreen = True;
    c->oldstate = c->isfloating;
    c->oldbw = c->bw;
    c->bw = 0;
    c->isfloating = True;
    resizeclient(c, c->mon->mx, c->mon->my, c->mon->mw, c->mon->mh);
    XRaiseWindow(dpy, c->win);
  } else if (!fullscreen && c->isfullscreen) {
    XChangeProperty(dpy, c->win, netatom[NetWMState], XA_ATOM, 32,
        PropModeReplace, (unsigned char*)0, 0);
    c->isfullscreen = False;
    c->isfloating = c->oldstate;
    c->bw = c->oldbw;
    c->x = c->oldx;
    c->y = c->oldy;
    c->w = c->oldw;
    c->h = c->oldh;
    resizeclient(c, c->x, c->y, c->w, c->h);
    arrange(c->mon);
  }
}

void
setlayout(const Arg *arg) {
  if (!arg || !arg->v || arg->v != selmon->lt[selmon->sellt]) {
    selmon->pertag->sellts[selmon->pertag->curtag] ^= 1;
    selmon->sellt = selmon->pertag->sellts[selmon->pertag->curtag];
  }
  if (arg && arg->v)
    selmon->pertag->ltidxs[selmon->pertag->curtag][selmon->sellt] = (Layout *)arg->v;
  selmon->lt[selmon->sellt] = selmon->pertag->ltidxs[selmon->pertag->curtag][selmon->sellt];
  strncpy(selmon->ltsymbol, selmon->lt[selmon->sellt]->symbol, sizeof selmon->ltsymbol);
  if (selmon->sel)
    arrange(selmon);
  else
    drawbar(selmon);
}

void
setup(void) {
  int i;
  XSetWindowAttributes wa;
  Atom utf8string;

  /* clean up any zombies immediately */
  sigchld(0);

  /* init screen */
  screen = DefaultScreen(dpy);
  sw = DisplayWidth(dpy, screen);
  sh = DisplayHeight(dpy, screen);
  root = RootWindow(dpy, screen);
  drw = drw_create(dpy, screen, root, sw, sh);
  if (!drw_fontset_create(drw, fonts, LENGTH(fonts)))
    die("no fonts could be loaded.");
  lrpad = drw->fonts->h;
  bh = lrpad + 2;
  updategeom();
  /* init atoms */
  utf8string = XInternAtom(dpy, "UTF8_STRING", False);
  wmatom[WMProtocols] = XInternAtom(dpy, "WM_PROTOCOLS", False);
  wmatom[WMDelete] = XInternAtom(dpy, "WM_DELETE_WINDOW", False);
  wmatom[WMState] = XInternAtom(dpy, "WM_STATE", False);
  wmatom[WMTakeFocus] = XInternAtom(dpy, "WM_TAKE_FOCUS", False);
  netatom[NetActiveWindow] = XInternAtom(dpy, "_NET_ACTIVE_WINDOW", False);
  netatom[NetSupported] = XInternAtom(dpy, "_NET_SUPPORTED", False);
  netatom[NetSystemTray] = XInternAtom(dpy, "_NET_SYSTEM_TRAY_S0", False);
  netatom[NetSystemTrayOP] = XInternAtom(dpy, "_NET_SYSTEM_TRAY_OPCODE", False);
  netatom[NetSystemTrayOrientation] = XInternAtom(dpy, "_NET_SYSTEM_TRAY_ORIENTATION", False);
  netatom[NetWMName] = XInternAtom(dpy, "_NET_WM_NAME", False);
  netatom[NetWMCheck] = XInternAtom(dpy, "_NET_SUPPORTING_WM_CHECK", False);
  netatom[NetWMState] = XInternAtom(dpy, "_NET_WM_STATE", False);
  netatom[NetWMFullscreen] = XInternAtom(dpy, "_NET_WM_STATE_FULLSCREEN", False);
  netatom[NetWMWindowType] = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE", False);
  netatom[NetWMWindowTypeDialog] = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_DIALOG", False);
  netatom[NetClientList] = XInternAtom(dpy, "_NET_CLIENT_LIST", False);
  xatom[Manager] = XInternAtom(dpy, "MANAGER", False);
  xatom[Xembed] = XInternAtom(dpy, "_XEMBED", False);
  xatom[XembedInfo] = XInternAtom(dpy, "_XEMBED_INFO", False);
  /* init cursors */
  cursor[CurNormal] = drw_cur_create(drw, XC_left_ptr);
  cursor[CurResizeBottomLeft] = drw_cur_create(drw, XC_bottom_left_corner);
  cursor[CurResizeBottomRight] = drw_cur_create(drw, XC_bottom_right_corner);
  cursor[CurResizeTopLeft] = drw_cur_create(drw, XC_top_left_corner);
  cursor[CurResizeTopRight] = drw_cur_create(drw, XC_top_right_corner);
  cursor[CurMove] = drw_cur_create(drw, XC_fleur);
  /* init appearance */
  scheme = ecalloc(LENGTH(colors), sizeof(Clr *));
  for (i = 0; i < LENGTH(colors); i++)
    scheme[i] = drw_scm_create(drw, colors[i], 3);
  /* init system tray */
  updatesystray();
  /* init bars */
  updatebars();
  updatestatus();
  /* supporting window for NetWMCheck */
  wmcheckwin = XCreateSimpleWindow(dpy, root, 0, 0, 1, 1, 0, 0, 0);
  XChangeProperty(dpy, wmcheckwin, netatom[NetWMCheck], XA_WINDOW, 32,
    PropModeReplace, (unsigned char *) &wmcheckwin, 1);
  XChangeProperty(dpy, wmcheckwin, netatom[NetWMName], utf8string, 8,
    PropModeReplace, (unsigned char *) "dwm", 3);
  XChangeProperty(dpy, root, netatom[NetWMCheck], XA_WINDOW, 32,
    PropModeReplace, (unsigned char *) &wmcheckwin, 1);
  /* EWMH support per view */
  XChangeProperty(dpy, root, netatom[NetSupported], XA_ATOM, 32,
      PropModeReplace, (unsigned char *) netatom, NetLast);
  /* init client list */
  updateclientlist();

  /* select for events */
  wa.cursor = cursor[CurNormal]->cursor;
  wa.event_mask = SubstructureRedirectMask|SubstructureNotifyMask
    |ButtonPressMask|PointerMotionMask|EnterWindowMask
    |LeaveWindowMask|StructureNotifyMask|PropertyChangeMask;
  XChangeWindowAttributes(dpy, root, CWEventMask|CWCursor, &wa);
  XSelectInput(dpy, root, wa.event_mask);
  grabkeys();
  focus(NULL);
}

void
seturgent(Client *c, Bool urg) {
  XWMHints *wmh;

  c->isurgent = urg;
  if (!(wmh = XGetWMHints(dpy, c->win)))
    return;
  wmh->flags = urg ? (wmh->flags | XUrgencyHint) : (wmh->flags & ~XUrgencyHint);
  XSetWMHints(dpy, c->win, wmh);
  XFree(wmh);
}

void
showhide(Client *c) {
  if (!c)
    return;
  if (ISVISIBLE(c)) {
    /* show clients top down */
    XMoveWindow(dpy, c->win, c->x, c->y);
    if ((!c->mon->lt[c->mon->sellt]->arrange || c->isfloating) && !c->isfullscreen)
      resize(c, c->x, c->y, c->w, c->h, False);
    showhide(c->snext);
  } else {
    /* hide clients bottom up */
    showhide(c->snext);
    XMoveWindow(dpy, c->win, WIDTH(c) * -2, c->y);
  }
}

void
sigchld(int unused) {
  if (signal(SIGCHLD, sigchld) == SIG_ERR)
    die("Can't install SIGCHLD handler");
  while (0 < waitpid(-1, NULL, WNOHANG));
}

void
spawn(const Arg *arg) {
  if (fork() == 0) {
    if (dpy)
      close(ConnectionNumber(dpy));
    setsid();
    execvp(((char **)arg->v)[0], (char **)arg->v);
    fprintf(stderr, "dwm: execvp %s", ((char **)arg->v)[0]);
    perror(" failed");
    exit(EXIT_SUCCESS);
  }
}

void
tag(const Arg *arg) {
  if (selmon->sel && arg->ui & TAGMASK) {
    selmon->sel->tags = arg->ui & TAGMASK;
    focus(NULL);
    arrange(selmon);
  }
}

void
tagmon(const Arg *arg) {
  if (!selmon->sel || !mons->next)
    return;
  sendmon(selmon->sel, dirtomon(arg->i));
}

void
tile(Monitor *m) {
  Client *c;
  Area *ga = m->pertag->areas[m->pertag->curtag], *ma = ga + 1, *sa = ga + 2, *a;
  unsigned int n, i, w, h, ms, ss;
  float f;

  /* print layout symbols */
  snprintf(m->ltsymbol, sizeof m->ltsymbol, "%c%c%c",
      (char[]){ '<', '^', '>', 'v' }[ga->dir],
      (char[]){ '-', '|' }[ma->dir],
      (char[]){ '-', '|' }[sa->dir]);
  /* calculate number of clients */
  for (n = 0, c = nexttiled(m->clients); c; c = nexttiled(c->next), n++);
  if (n == 0)
    return;

  ma->n = MIN(n, m->nmaster), sa->n = n - ma->n;

  /* calculate area rectangles */
  f = ma->n == 0 ? 0 : (sa->n == 0 ? 1 : ga->fact / 2);
  if (ga->dir == DirHor || ga->dir == DirRotHor) {
    ms = f * m->ww;
    ss = m->ww - ms;

    ma->x = ga->dir == DirHor ? 0 : ss;
    ma->y = 0;
    ma->fx = ma->x + ms;
    ma->fy = m->wh;

    sa->x = ga->dir == DirHor ? ms : 0;
    sa->y = 0;
    sa->fx = sa->x + ss;
    sa->fy = m->wh;
  } else {
    ms = f * m->wh;
    ss = m->wh - ms,
    ma->x = 0;
    ma->y = ga->dir == DirVer ? 0 : ss, ma->fx = m->ww, ma->fy = ma->y + ms,
    sa->x = 0;
    sa->y = ga->dir == DirVer ? ms : 0, sa->fx = m->ww, sa->fy = sa->y + ss;
  }
  /* tile clients */
  for (c = nexttiled(m->clients), i = 0; i < n; c = nexttiled(c->next), i++) {
    a = ma->n > 0 ? ma : sa;
    f = i == 0 || ma->n == 0 ? a->fact : 1, f /= --a->n + f;
    w = (a->dir == DirVer ? 1 : f) * (a->fx - a->x);
    h = (a->dir == DirHor ? 1 : f) * (a->fy - a->y);
    resize(c, m->wx + a->x, m->wy + a->y, w - 2 * c->bw, h - 2 * c->bw, False);
    a->x += a->dir == DirHor ? w : 0;
    a->y += a->dir == DirVer ? h : 0;
  }
}

void
togglebar(const Arg *arg) {
  selmon->showbar = selmon->pertag->showbars[selmon->pertag->curtag] = !selmon->showbar;
  updatebarpos(selmon);
  resizebarwin(selmon);
  if (showsystray) {
    XWindowChanges wc;
    if (!selmon->showbar)
      wc.y = -bh;
    else if (selmon->showbar) {
      wc.y = 0;
      if (!selmon->topbar)
        wc.y = selmon->mh - bh;
    }
    XConfigureWindow(dpy, systray->win, CWY, &wc);
  }
  arrange(selmon);
}

void
togglefloating(const Arg *arg) {
  if (!selmon->sel)
    return;
  if (selmon->sel->isfullscreen) /* no support for fullscreen windows */
    return;
  selmon->sel->isfloating = !selmon->sel->isfloating || selmon->sel->isfixed;
  if (selmon->sel->isfloating)
    resize(selmon->sel, selmon->sel->x, selmon->sel->y,
        selmon->sel->w, selmon->sel->h, False);
  arrange(selmon);
}

void
togglefullscreen(const Arg *arg) {
  if (!selmon->sel)
    return;
  setfullscreen(selmon->sel, !selmon->sel->isfullscreen);
}

void
toggletag(const Arg *arg) {
  unsigned int newtags;

  if (!selmon->sel)
    return;
  newtags = selmon->sel->tags ^ (arg->ui & TAGMASK);
  if (newtags) {
    selmon->sel->tags = newtags;
    focus(NULL);
    arrange(selmon);
  }
}

void
toggleview(const Arg *arg) {
  unsigned int newtagset = selmon->tagset[selmon->seltags] ^ (arg->ui & TAGMASK);
  int i;

  if (newtagset) {
    if (newtagset == ~0) {
      selmon->pertag->prevtag = selmon->pertag->curtag;
      selmon->pertag->curtag = 0;
    }
    /* test if the user did not select the same tag */
    if (!(newtagset & 1 << (selmon->pertag->curtag - 1))) {
      selmon->pertag->prevtag = selmon->pertag->curtag;
      for (i=0; !(newtagset & 1 << i); i++) ;
      selmon->pertag->curtag = i + 1;
    }
    selmon->tagset[selmon->seltags] = newtagset;

    /* apply settings for this view */
    selmon->nmaster = selmon->pertag->nmasters[selmon->pertag->curtag];
    selmon->sellt = selmon->pertag->sellts[selmon->pertag->curtag];
    selmon->lt[selmon->sellt] = selmon->pertag->ltidxs[selmon->pertag->curtag][selmon->sellt];
    selmon->lt[selmon->sellt^1] = selmon->pertag->ltidxs[selmon->pertag->curtag][selmon->sellt^1];
    if (selmon->showbar != selmon->pertag->showbars[selmon->pertag->curtag])
      togglebar(NULL);
    focus(NULL);
    arrange(selmon);
  }
}

void
unfocus(Client *c, Bool setfocus) {
  if (!c)
    return;
  grabbuttons(c, False);
  XSetWindowBorder(dpy, c->win, scheme[SchemeNorm][ColBorder].pixel);
  if (setfocus) {
    XSetInputFocus(dpy, root, RevertToPointerRoot, CurrentTime);
    XDeleteProperty(dpy, root, netatom[NetActiveWindow]);
  }
}

void
unmanage(Client *c, Bool destroyed) {
  Monitor *m = c->mon;
  XWindowChanges wc;

  if (c->swallowing) {
    unswallow(c);
    return;
  }

  Client *s = swallowingclient(c->win);
  if (s) {
    free(s->swallowing);
    s->swallowing = NULL;
    arrange(m);
    focus(NULL);
    return;
  }

  /* The server grab construct avoids race conditions. */
  detach(c);
  detachstack(c);
  if (!destroyed) {
    wc.border_width = c->oldbw;
    XGrabServer(dpy); /* avoid race conditions */
    XSetErrorHandler(xerrordummy);
    XConfigureWindow(dpy, c->win, CWBorderWidth, &wc); /* restore border */
    XUngrabButton(dpy, AnyButton, AnyModifier, c->win);
    setclientstate(c, WithdrawnState);
    XSync(dpy, False);
    XSetErrorHandler(xerror);
    XUngrabServer(dpy);
  }
  free(c);

  if (!s) {
    arrange(m);
    focus(NULL);
    updateclientlist();
  }
}

void
unmapnotify(XEvent *e) {
  Client *c;
  XUnmapEvent *ev = &e->xunmap;

  if ((c = wintoclient(ev->window))) {
    if (ev->send_event)
      setclientstate(c, WithdrawnState);
    else
      unmanage(c, False);
  }
  else if ((c = wintosystrayicon(ev->window))) {
    removesystrayicon(c);
    resizebarwin(selmon);
    updatesystray();
  }
  updateclientlist();
}

void
updatebars(void) {
  unsigned int w;
  Monitor *m;
  XSetWindowAttributes wa = {
    .override_redirect = True,
    .background_pixmap = ParentRelative,
    .event_mask = ButtonPressMask|ExposureMask
  };
  XClassHint ch = {"dwm", "dwm"};
  for (m = mons; m; m = m->next) {
    if (m->barwin)
      continue;
    w = m->ww;
    if (showsystray && m == selmon)
      w -= getsystraywidth();
    m->barwin = XCreateWindow(dpy, root, m->wx, m->by, w, bh, 0, DefaultDepth(dpy, screen),
        CopyFromParent, DefaultVisual(dpy, screen),
        CWOverrideRedirect|CWBackPixmap|CWEventMask, &wa);
    XDefineCursor(dpy, m->barwin, cursor[CurNormal]->cursor);
    if (showsystray && m == selmon)
      XMapRaised(dpy, m->barwin);
    else
      XMapWindow(dpy, m->barwin);
    XSetClassHint(dpy, m->barwin, &ch);
  }
}

void
updatebarpos(Monitor *m) {
  m->wy = m->my;
  m->wh = m->mh;
  if (m->showbar) {
    m->wh -= bh;
    m->by = m->topbar ? m->wy : m->wy + m->wh;
    m->wy = m->topbar ? m->wy + bh : m->wy;
  } else
    m->by = -bh;
}

void
updateclientlist() {
  Client *c;
  Monitor *m;

  XDeleteProperty(dpy, root, netatom[NetClientList]);
  for (m = mons; m; m = m->next)
    for (c = m->clients; c; c = c->next)
      XChangeProperty(dpy, root, netatom[NetClientList],
        XA_WINDOW, 32, PropModeAppend,
        (unsigned char *) &(c->win), 1);
}

Bool
updategeom(void) {
  Bool dirty = False;

#ifdef XINERAMA
  if (XineramaIsActive(dpy)) {
    int i, j, n, nn;
    Client *c;
    Monitor *m;
    XineramaScreenInfo *info = XineramaQueryScreens(dpy, &nn);
    XineramaScreenInfo *unique = NULL;

    for (n = 0, m = mons; m; m = m->next, n++);
    /* only consider unique geometries as separate screens */
    unique = ecalloc(nn, sizeof(XineramaScreenInfo));
    for (i = 0, j = 0; i < nn; i++)
      if (isuniquegeom(unique, j, &info[i]))
        memcpy(&unique[j++], &info[i], sizeof(XineramaScreenInfo));
    XFree(info);
    nn = j;
    if (n <= nn) { /* new monitors available */
      for (i = 0; i < (nn - n); i++) {
        for (m = mons; m && m->next; m = m->next);
        if (m)
          m->next = createmon();
        else
          mons = createmon();
      }
      for (i = 0, m = mons; i < nn && m; m = m->next, i++)
        if (i >= n
            || unique[i].x_org != m->mx || unique[i].y_org != m->my
            || unique[i].width != m->mw || unique[i].height != m->mh)
        {
          dirty = True;
          m->num = i;
          m->mx = m->wx = unique[i].x_org;
          m->my = m->wy = unique[i].y_org;
          m->mw = m->ww = unique[i].width;
          m->mh = m->wh = unique[i].height;
          updatebarpos(m);
        }
    } else { /* less monitors available nn < n */
      for (i = nn; i < n; i++) {
        for (m = mons; m && m->next; m = m->next);
        while ((c = m->clients)) {
          dirty = True;
          m->clients = c->next;
          detachstack(c);
          c->mon = mons;
          attach(c);
          attachstack(c);
        }
        if (m == selmon)
          selmon = mons;
        cleanupmon(m);
      }
    }
    free(unique);
  } else
#endif /* XINERAMA */
  { /* default monitor setup */
    if (!mons)
      mons = createmon();
    if (mons->mw != sw || mons->mh != sh) {
      dirty = True;
      mons->mw = mons->ww = sw;
      mons->mh = mons->wh = sh;
      updatebarpos(mons);
    }
  }
  if (dirty) {
    selmon = mons;
    selmon = wintomon(root);
  }
  return dirty;
}

void
updatenumlockmask(void) {
  unsigned int i, j;
  XModifierKeymap *modmap;

  numlockmask = 0;
  modmap = XGetModifierMapping(dpy);
  for (i = 0; i < 8; i++)
    for (j = 0; j < modmap->max_keypermod; j++)
      if (modmap->modifiermap[i * modmap->max_keypermod + j]
        == XKeysymToKeycode(dpy, XK_Num_Lock))
        numlockmask = (1 << i);
  XFreeModifiermap(modmap);
}

void
updatesizehints(Client *c) {
  long msize;
  XSizeHints size;

  if (!XGetWMNormalHints(dpy, c->win, &size, &msize))
    /* size is uninitialized, ensure that size.flags aren't used */
    size.flags = PSize;
  if (size.flags & PBaseSize) {
    c->basew = size.base_width;
    c->baseh = size.base_height;
  } else if (size.flags & PMinSize) {
    c->basew = size.min_width;
    c->baseh = size.min_height;
  } else
    c->basew = c->baseh = 0;
  if (size.flags & PResizeInc) {
    c->incw = size.width_inc;
    c->inch = size.height_inc;
  } else
    c->incw = c->inch = 0;
  if (size.flags & PMaxSize) {
    c->maxw = size.max_width;
    c->maxh = size.max_height;
  } else
    c->maxw = c->maxh = 0;
  if (size.flags & PMinSize) {
    c->minw = size.min_width;
    c->minh = size.min_height;
  } else if (size.flags & PBaseSize) {
    c->minw = size.base_width;
    c->minh = size.base_height;
  } else
    c->minw = c->minh = 0;
  if (size.flags & PAspect) {
    c->mina = (float)size.min_aspect.y / size.min_aspect.x;
    c->maxa = (float)size.max_aspect.x / size.max_aspect.y;
  } else
    c->maxa = c->mina = 0.0;
  c->isfixed = (c->maxw && c->maxh && c->maxw == c->minw && c->maxh == c->minh);
}

void
updatestatus(void) {
  if (!gettextprop(root, XA_WM_NAME, stext, sizeof(stext)))
    strcpy(stext, "dwm-"VERSION);
  drawbar(selmon);
  updatesystray();
}

void
updatetitle(Client *c) {
  if (!gettextprop(c->win, netatom[NetWMName], c->name, sizeof c->name))
    gettextprop(c->win, XA_WM_NAME, c->name, sizeof c->name);
  if (c->name[0] == '\0') /* hack to mark broken clients */
    strcpy(c->name, broken);
}


void
updatesystrayicongeom(Client *i, int w, int h) {
  if (i) {
    i->h = bh;
    if (w == h)
      i->w = bh;
    else if (h == bh)
      i->w = w;
    else
      i->w = (int) ((float)bh * ((float)w / (float)h));
    applysizehints(i, &(i->x), &(i->y), &(i->w), &(i->h), False);
    /* force icons into the systray dimenons if they don't want to */
    if (i->h > bh) {
      if (i->w == i->h)
        i->w = bh;
      else
        i->w = (int) ((float)bh * ((float)i->w / (float)i->h));
      i->h = bh;
    }
  }
}

void
updatesystrayiconstate(Client *i, XPropertyEvent *ev) {
  long flags;
  int code = 0;

  if (!showsystray || !i || ev->atom != xatom[XembedInfo] ||
      !(flags = getatomprop(i, xatom[XembedInfo])))
    return;

  if (flags & XEMBED_MAPPED && !i->tags) {
    i->tags = 1;
    code = XEMBED_WINDOW_ACTIVATE;
    XMapRaised(dpy, i->win);
    setclientstate(i, NormalState);
  }
  else if (!(flags & XEMBED_MAPPED) && i->tags) {
    i->tags = 0;
    code = XEMBED_WINDOW_DEACTIVATE;
    XUnmapWindow(dpy, i->win);
    setclientstate(i, WithdrawnState);
  }
  else
    return;
  sendevent(i->win, xatom[Xembed], StructureNotifyMask, CurrentTime, code, 0,
      systray->win, XEMBED_EMBEDDED_VERSION);
}

void
updatesystray(void) {
  XSetWindowAttributes wa;
  XWindowChanges wc;
  Client *i;
  unsigned int x = selmon->mx + selmon->mw;
  unsigned int w = 1;

  if (!showsystray)
    return;
  if (!systray) {
    /* init systray */
    systray = ecalloc(1, sizeof(Systray));
    systray->win = XCreateSimpleWindow(dpy, root, x, selmon->by, w, bh, 0, 0, scheme[SchemeSel][ColBg].pixel);
    wa.event_mask        = ButtonPressMask | ExposureMask;
    wa.override_redirect = True;
    wa.background_pixmap = ParentRelative;
    wa.background_pixel  = scheme[SchemeNorm][ColBg].pixel;
    XSelectInput(dpy, systray->win, SubstructureNotifyMask);
    XChangeProperty(dpy, systray->win, netatom[NetSystemTrayOrientation], XA_CARDINAL, 32,
        PropModeReplace, (unsigned char *)&systrayorientation, 1);
    XChangeWindowAttributes(dpy, systray->win, CWEventMask|CWOverrideRedirect|CWBackPixel|CWBackPixmap, &wa);
    XMapRaised(dpy, systray->win);
    XSetSelectionOwner(dpy, netatom[NetSystemTray], systray->win, CurrentTime);
    if (XGetSelectionOwner(dpy, netatom[NetSystemTray]) == systray->win) {
      sendevent(root, xatom[Manager], StructureNotifyMask, CurrentTime, netatom[NetSystemTray], systray->win, 0, 0);
      XSync(dpy, False);
    }
    else {
      fprintf(stderr, "dwm: unable to obtain system tray.\n");
      free(systray);
      systray = NULL;
      return;
    }
  }
  for (w = 0, i = systray->icons; i; i = i->next) {
    /* make the background color stay the same */
    wa.background_pixel = scheme[SchemeNorm][ColBg].pixel;
    XChangeWindowAttributes(dpy, i->win, CWBackPixel, &wa);
    XMapRaised(dpy, i->win);

    w += systrayspacing;
    i->x = w;
    XMoveResizeWindow(dpy, i->win, i->x, 0, i->w, i->h);
    w += i->w;
    if (i->mon != selmon)
      i->mon = selmon;
  }
  w = w ? w + systrayspacing : 1;
  x -= w;
  XMoveResizeWindow(dpy, systray->win, x, selmon->by, w, bh);
  wc.x = x;
  wc.y = selmon->by;
  wc.width = w;
  wc.height = bh;
  wc.stack_mode = Above;
  wc.sibling = selmon->barwin;
  XConfigureWindow(dpy, systray->win, CWX|CWY|CWWidth|CWHeight|CWStackMode|CWSibling, &wc);
  XMapWindow(dpy, systray->win);
  XMapSubwindows(dpy, systray->win);

  /* redraw background */
  drw_setscheme(drw, scheme[SchemeNorm]);
  drw_rect(drw, 0, 0, w, bh, 1, 0);
  drw_map(drw, systray->win, x, 0, w, bh);
}

void
updatewindowtype(Client *c) {
  Atom state = getatomprop(c, netatom[NetWMState]);
  Atom wtype = getatomprop(c, netatom[NetWMWindowType]);

  if (state == netatom[NetWMFullscreen])
    setfullscreen(c, True);

  if (wtype == netatom[NetWMWindowTypeDialog])
    c->isfloating = True;
}

void
updatewmhints(Client *c) {
  XWMHints *wmh;

  if ((wmh = XGetWMHints(dpy, c->win))) {
    if (c == selmon->sel && wmh->flags & XUrgencyHint) {
      wmh->flags &= ~XUrgencyHint;
      XSetWMHints(dpy, c->win, wmh);
    } else
      c->isurgent = (wmh->flags & XUrgencyHint) ? True : False;
    if (wmh->flags & InputHint)
      c->neverfocus = !wmh->input;
    else
      c->neverfocus = False;
    XFree(wmh);
  }
}


void
view(const Arg *arg) {
  int i;
  unsigned int tmptag;

  if ((arg->ui & TAGMASK) == selmon->tagset[selmon->seltags])
    return;
  selmon->seltags ^= 1; /* toggle sel tagset */
  if (arg->ui & TAGMASK) {
    selmon->pertag->prevtag = selmon->pertag->curtag;
    selmon->tagset[selmon->seltags] = arg->ui & TAGMASK;
    if (arg->ui == ~0)
      selmon->pertag->curtag = 0;
    else {
      for (i=0; !(arg->ui & 1 << i); i++) ;
      selmon->pertag->curtag = i + 1;
    }
  } else {
    tmptag = selmon->pertag->prevtag;
    selmon->pertag->prevtag = selmon->pertag->curtag;
    selmon->pertag->curtag = tmptag;
  }
  selmon->nmaster = selmon->pertag->nmasters[selmon->pertag->curtag];
  selmon->sellt = selmon->pertag->sellts[selmon->pertag->curtag];
  selmon->lt[selmon->sellt] = selmon->pertag->ltidxs[selmon->pertag->curtag][selmon->sellt];
  selmon->lt[selmon->sellt^1] = selmon->pertag->ltidxs[selmon->pertag->curtag][selmon->sellt^1];
  if (selmon->showbar != selmon->pertag->showbars[selmon->pertag->curtag])
    togglebar(NULL);
  focus(NULL);
  arrange(selmon);
}

pid_t winpid(Window w) {
  pid_t result = 0;
  xcb_res_client_id_spec_t spec = {0};
  spec.client = w;

  spec.mask = XCB_RES_CLIENT_ID_MASK_LOCAL_CLIENT_PID;

  xcb_generic_error_t *e = NULL;
  xcb_res_query_client_ids_cookie_t c = xcb_res_query_client_ids(xcon, 1, &spec);
  xcb_res_query_client_ids_reply_t *r = xcb_res_query_client_ids_reply(xcon, c, &e);

  if (!r)
    return result;

  xcb_res_client_id_value_iterator_t it = xcb_res_query_client_ids_ids_iterator(r);
  for (; it.rem; xcb_res_client_id_value_next(&it)) {
    spec = it.data->spec;
    if (spec.mask & XCB_RES_CLIENT_ID_MASK_LOCAL_CLIENT_PID) {
      uint32_t *t = xcb_res_client_id_value_value(it.data);
      result = *t;
      break;
    }
  }

  free(r);

  if (result < 0)
    result = 0;
  return result;
}

pid_t
getparentprocess(pid_t p) {
  unsigned int v = 0;
#if defined(__linux__)
  FILE *f;
  char buf[256];

  snprintf(buf, sizeof(buf) - 1, "/proc/%u/stat", p);
  if (!(f = fopen(buf, "r"))) {
    return (pid_t)0;
  }

  fscanf(f, "%*u %*s %*c %u", &v);
  fclose(f);
#elif defined(__FreeBSD__)
  struct kinfo_proc *proc = kinfo_getproc(p);
  if (!proc)
    return (pid_t)0;

  v = proc->ki_ppid;
  free(proc);
#endif
  return (pid_t)v;
}

Client *
wintoclient(Window w) {
  Client *c;
  Monitor *m;

  for (m = mons; m; m = m->next)
    for (c = m->clients; c; c = c->next)
      if (c->win == w)
        return c;
  return NULL;
}

int
isdescprocess(pid_t p, pid_t c) {
  while (p != c && c != 0)
    c = getparentprocess(c);
  return (int)c;
}

Client *
termforwin(const Client *w) {
  Client *c;
  Monitor *m;

  if (!w->pid || w->isterminal)
    return NULL;

  for (m = mons; m; m = m->next) {
    for (c = m->clients; c; c = c->next) {
      if (c->isterminal && !c->swallowing && c->pid && isdescprocess(c->pid, w->pid))
        return c;
    }
  }

  return NULL;
}

Client *
swallowingclient(Window w) {
  Client *c;
  Monitor *m;

  for (m = mons; m; m = m->next) {
    for (c = m->clients; c; c = c->next) {
      if (c->swallowing && c->swallowing->win == w)
        return c;
    }
  }

  return NULL;
}

Monitor *
wintomon(Window w) {
  int x, y;
  Client *c;
  Monitor *m;

  if (w == root && getrootptr(&x, &y))
    return recttomon(x, y, 1, 1);
  for (m = mons; m; m = m->next)
    if (w == m->barwin)
      return m;
  if ((c = wintoclient(w)))
    return c->mon;
  return selmon;
}

Client *
wintosystrayicon(Window w) {
  Client *i = NULL;

  if (!showsystray || !w)
    return i;
  for (i = systray->icons; i && i->win != w; i = i->next) ;
  return i;
}

/* There's no way to check accesses to destroyed windows, thus those cases are
 * ignored (especially on UnmapNotify's).  Other types of errors call Xlibs
 * default error handler, which may call exit.  */
int
xerror(Display *dpy, XErrorEvent *ee) {
  if (ee->error_code == BadWindow
      || (ee->request_code == X_SetInputFocus && ee->error_code == BadMatch)
      || (ee->request_code == X_PolyText8 && ee->error_code == BadDrawable)
      || (ee->request_code == X_PolyFillRectangle && ee->error_code == BadDrawable)
      || (ee->request_code == X_PolySegment && ee->error_code == BadDrawable)
      || (ee->request_code == X_ConfigureWindow && ee->error_code == BadMatch)
      || (ee->request_code == X_GrabButton && ee->error_code == BadAccess)
      || (ee->request_code == X_GrabKey && ee->error_code == BadAccess)
      || (ee->request_code == X_CopyArea && ee->error_code == BadDrawable))
    return 0;
  fprintf(stderr, "dwm: fatal error: request code=%d, error code=%d\n",
      ee->request_code, ee->error_code);
  return xerrorxlib(dpy, ee); /* may call exit */
}

int
xerrordummy(Display *dpy, XErrorEvent *ee) {
  return 0;
}

/* Startup Error handler to check if another window manager
 * is already running. */
int
xerrorstart(Display *dpy, XErrorEvent *ee) {
  die("dwm: another window manager is already running\n");
  return -1;
}

void
zoom(const Arg *arg) {
  Client *c = selmon->sel;

  if (!selmon->lt[selmon->sellt]->arrange
      || (selmon->sel && selmon->sel->isfloating))
    return;
  if (c == nexttiled(selmon->clients))
    if (!c || !(c = nexttiled(c->next)))
      return;
  pop(c);
}

int
main(int argc, char *argv[]) {
  if (argc == 2 && !strcmp("-v", argv[1]))
    die("dwm-"VERSION);
  else if (argc != 1)
    die("usage: dwm [-v]");
  if (!setlocale(LC_CTYPE, "") || !XSupportsLocale())
    fputs("warning: no locale support\n", stderr);
  if (!(dpy = XOpenDisplay(NULL)))
    die("dwm: cannot open display");
  if (!(xcon = XGetXCBConnection(dpy)))
    die("dwm: cannot get xcb connection\n");
  checkotherwm();
  setup();
#ifdef __OpenBSD__
  if (pledge("stdio rpath proc exec", NULL) == -1)
    die("pledge");
#endif /* __OpenBSD__ */
  scan();
  run();
  cleanup();
  XCloseDisplay(dpy);
  return EXIT_SUCCESS;
}
