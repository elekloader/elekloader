/* SPDX-License-Identifier: GPL-2.0-or-later */
/* The machine menu's filter tab (FUNC+SRC, then RIGHT), as on the Digitakt II. A row is the stock menu's
 * own item, so the list draws, moves and frees it; the stock view knows nothing of the tab. The view's
 * addresses are the same on OS 1.53 and 1.54; what it calls is in mod.json's defsym. */
#include <stdint.h>

#define KEY_YES   12
#define KEY_NO    13
#define KEY_LEFT  16
#define KEY_RIGHT 17
#define FILTER_STOCK 0x100                       /* a row's id past every machine: the stock filter */

/* The view's fields, and a key event's. */
#define PAGE_FILTER 2                            /* the FLTR page, in a track's current page */
#define P_APP     164                            /* the page views' owner's field: the app's tracks */
#define S_VIEW    420                            /* the SRC page's field: its machine menu */
#define V_LIST    76
#define V_CURRENT 416                            /* the machine the menu opened on */
#define V_MACHINE 436                            /* the machine at the cursor */
#define V_STEP    480                            /* the UP, DOWN and LEVEL step's invoker */
#define E_KEY     12
#define E_FLAGS   16
#define E_TAPPED  0x10                           /* the edge stock's YES acts on */
#define FIELD(p, off) (*(uint32_t *)((char *)(p) + (off)))

struct fn { void *data, *pad, *manager, *invoker; };     /* the firmware's std::function */
struct vec { int32_t *begin, *end, *cap; };

extern void *MENU_NEW(uint32_t);
extern void MENU_ITEM(void *, struct fn *, struct fn *, struct fn *, struct fn *, int32_t, int32_t);
extern void MENU_FN_FREE(struct fn *);
extern void MENU_ADD(void *, void *);
extern void MENU_CLEAR(void *);
extern void MENU_CURSOR(void *, int32_t, int32_t);
extern void MENU_VEC_FREE(void *);
extern uint32_t MENU_KEY_FN(void *, void *);
extern void MENU_CLOSE(void *, int32_t);
extern void MENU_PAGE_STEP(void *, int32_t, int32_t);
extern void MENU_SHOW(void *);
extern int32_t MENU_PAGE_FN(void *, int32_t);
extern const char *MENU_NAME_FN(int32_t);
extern void MENU_HLINE(void *, int32_t, int32_t, int32_t, int32_t);
extern void MENU_BLIT(void *, void *, int32_t, int32_t, int32_t);
extern void MENU_TEXT(void *, void *, int32_t, int32_t, int32_t, const char *);
extern char MENU_WAVE[], MENU_FONT[];
extern char MENU_NAME_MGR[], MENU_NAME_CALL[], MENU_ICON_MGR[], MENU_ICON_CALL[];
extern void *APP_FN(void);
extern void *APP_TRACKS(void *);
extern int32_t SRC_TRACK_FN(void *);
extern void SRC_DIRTY_FN(void *);
extern void mp_menu_list(struct vec *, int32_t);         /* menu.s: the machines a track offers */
extern void mp_menu_no_step(void);                       /* menu.s */

uint32_t mp_menu_filter;                                 /* the open menu is on its filter tab */
static uint32_t stock_step;
static uint32_t from_filter_page;
static void *page_owner;
void *mp_menu_back;                                      /* the page views' owner, when the open menu came from FLTR */

static struct fn view_fn(void *view, char *manager, char *invoker)
{
    void **closure = MENU_NEW(4);
    *closure = view;
    return (struct fn){closure, 0, manager, invoker};
}

static void add_row(void *view, int32_t id)
{
    struct fn name = view_fn(view, MENU_NAME_MGR, MENU_NAME_CALL);
    struct fn icon = view_fn(view, MENU_ICON_MGR, MENU_ICON_CALL);
    struct fn none1 = {0, 0, 0, 0}, none2 = {0, 0, 0, 0};
    void *item = MENU_NEW(0x54);
    MENU_ITEM(item, &name, &none1, &icon, &none2, id, 8);
    MENU_ADD((char *)view + V_LIST, item);
    MENU_FN_FREE(&none2);
    MENU_FN_FREE(&icon);
    MENU_FN_FREE(&none1);
    MENU_FN_FREE(&name);
}

static void show_filters(void *view)
{
    void *list = (char *)view + V_LIST;
    MENU_CLEAR(list);
    add_row(view, FILTER_STOCK);
    MENU_CURSOR(list, FILTER_STOCK, 0);
    /* The step moves the machine, whichever list shows; its manager stays for the view's destructor. */
    stock_step = FIELD(view, V_STEP);
    FIELD(view, V_STEP) = (uintptr_t)mp_menu_no_step;
    mp_menu_filter = 1;
}

static void show_machines(void *view)
{
    void *list = (char *)view + V_LIST;
    struct vec machines;
    MENU_CLEAR(list);
    mp_menu_list(&machines, SRC_TRACK_FN(APP_TRACKS(APP_FN())));
    for (int32_t *m = machines.begin; m != machines.end; ++m) {
        if (*m == -1)                                    /* as the view's constructor does */
            continue;
        add_row(view, *m);
        if ((uint32_t)*m == FIELD(view, V_MACHINE))
            MENU_CURSOR(list, *m, 0);
    }
    if (machines.begin)
        MENU_VEC_FREE(machines.begin);
    FIELD(view, V_STEP) = stock_step;
    mp_menu_filter = 0;
}

/* The view's key handler, by its vtable. Stock passes LEFT and RIGHT on, and nothing takes them. */
uint32_t mp_menu_key(void *view, void *event)
{
    uint32_t key = FIELD(event, E_KEY), tapped = FIELD(event, E_FLAGS) & E_TAPPED, taken;
    if (key == KEY_LEFT || key == KEY_RIGHT) {
        if (tapped && (key == KEY_RIGHT) != mp_menu_filter) {
            if (key == KEY_RIGHT)
                show_filters(view);
            else
                show_machines(view);
            SRC_DIRTY_FN(view);
        }
        return 1;
    }
    /* YES on the filter tab: a machine stepped to before it still commits; otherwise nothing is written. */
    if (mp_menu_filter && key == KEY_YES && tapped && FIELD(view, V_MACHINE) == FIELD(view, V_CURRENT)) {
        MENU_CLOSE(view, 0);
        taken = 1;
    } else {
        taken = MENU_KEY_FN(view, event);
    }
    /* YES and NO go back to a menu's FLTR page; a page key goes to its own page, as stock. */
    if (mp_menu_back && (key == KEY_YES || key == KEY_NO) && tapped) {
        MENU_PAGE_STEP(mp_menu_back, PAGE_FILTER, 0);
        mp_menu_back = 0;
    }
    return taken;
}

/* FUNC+SRC on any page but SRC, before it steps to the SRC page: was it the FLTR page? */
void mp_menu_src_key(void *owner)
{
    void *tracks = (void *)FIELD(owner, P_APP);
    page_owner = owner;
    int32_t track = SRC_TRACK_FN(tracks);
    from_filter_page = track < 8 && MENU_PAGE_FN(tracks, track) == PAGE_FILTER;  /* past 7, page 2 is another's */
}

/* ... and the menu it then opens, by the call's keep2: from the FLTR page, on its filter tab. */
void mp_menu_show(void *src_page)
{
    MENU_SHOW(src_page);
    if (from_filter_page) {
        void *view = (void *)FIELD(src_page, S_VIEW);
        mp_menu_back = page_owner;
        if (!mp_menu_filter) {
            show_filters(view);
            SRC_DIRTY_FN(view);
        }
    }
    from_filter_page = 0;
}

/* The filter tab's icon, as the Digitakt II's FLTR page: the curve's height over each of 26 columns. */
static const uint8_t curve[26] = {6, 6, 6, 6, 6, 6, 7, 7, 7, 8, 8, 9, 10, 11, 12, 14, 16, 18, 17, 14, 11, 8, 5, 3, 1, 0};

static void rect(void *screen, int32_t x, int32_t y, int32_t w, int32_t h)
{
    while (h--)
        MENU_HLINE(screen, x, y + h, x + w - 1, 1);
}

/* The panel beside the list. x and y count from the screen's bottom left, as the stock panel's do. */
void mp_menu_panel(void *screen)
{
    if (mp_menu_filter) {
        for (int32_t c = 0; c < 26; ++c) {
            int32_t next = curve[c < 25 ? c + 1 : c], gap = next > curve[c] ? next - curve[c] : curve[c] - next;
            rect(screen, 24 + c, 31 + (next < curve[c] ? next : curve[c]), 1, gap + 3);
        }
    } else {
        MENU_BLIT(screen, MENU_WAVE, 24, 29, 0);
    }
    MENU_TEXT(screen, MENU_FONT, 37, 11, 2, mp_menu_filter ? "< FLTR >" : "< SRC >");
    /* The page dots: the one shown is the larger. */
    rect(screen, mp_menu_filter ? 34 : 33, mp_menu_filter ? 5 : 4, mp_menu_filter ? 1 : 3, mp_menu_filter ? 1 : 3);
    rect(screen, mp_menu_filter ? 39 : 40, mp_menu_filter ? 4 : 5, mp_menu_filter ? 3 : 1, mp_menu_filter ? 3 : 1);
}

/* A row's name: the item's callback asks for the machine's. */
const char *mp_menu_name(int32_t id)
{
    return id == FILTER_STOCK ? "MULTI-MODE" : MENU_NAME_FN(id);
}
