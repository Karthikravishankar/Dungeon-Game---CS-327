#include <unistd.h>
#include <ncurses.h>
#include <ctype.h>
#include <stdlib.h>

#include "io.h"
#include "move.h"
#include "path.h"
#include "pc.h"
#include "utils.h"
#include "dungeon.h"
#include "object.h"
#include "npc.h"

/* Same ugly hack we did in path.c */
static dungeon *thedungeon;

typedef struct io_message {
  /* Will print " --more-- " at end of line when another message follows. *
   * Leave 10 extra spaces for that.                                      */
  char msg[71];
  struct io_message *next;
} io_message_t;

static io_message_t *io_head, *io_tail;

void io_init_terminal(void)
{
  initscr();
  raw();
  noecho();
  curs_set(0);
  keypad(stdscr, TRUE);
  start_color();
  init_pair(COLOR_RED, COLOR_RED, COLOR_BLACK);
  init_pair(COLOR_GREEN, COLOR_GREEN, COLOR_BLACK);
  init_pair(COLOR_YELLOW, COLOR_YELLOW, COLOR_BLACK);
  init_pair(COLOR_BLUE, COLOR_BLUE, COLOR_BLACK);
  init_pair(COLOR_MAGENTA, COLOR_MAGENTA, COLOR_BLACK);
  init_pair(COLOR_CYAN, COLOR_CYAN, COLOR_BLACK);
  init_pair(COLOR_WHITE, COLOR_WHITE, COLOR_BLACK);
}

void io_reset_terminal(void)
{
  endwin();

  while (io_head) {
    io_tail = io_head;
    io_head = io_head->next;
    free(io_tail);
  }
  io_tail = NULL;
}

void io_queue_message(const char *format, ...)
{
  io_message_t *tmp;
  va_list ap;

  if (!(tmp = (io_message_t *) malloc(sizeof (*tmp)))) {
    perror("malloc");
    exit(1);
  }

  tmp->next = NULL;

  va_start(ap, format);

  vsnprintf(tmp->msg, sizeof (tmp->msg), format, ap);

  va_end(ap);

  if (!io_head) {
    io_head = io_tail = tmp;
  } else {
    io_tail->next = tmp;
    io_tail = tmp;
  }
}

static void io_print_message_queue(uint32_t y, uint32_t x)
{
  while (io_head) {
    io_tail = io_head;
    attron(COLOR_PAIR(COLOR_CYAN));
    mvprintw(y, x, "%-80s", io_head->msg);
    attroff(COLOR_PAIR(COLOR_CYAN));
    io_head = io_head->next;
    if (io_head) {
      attron(COLOR_PAIR(COLOR_CYAN));
      mvprintw(y, x + 70, "%10s", " --more-- ");
      attroff(COLOR_PAIR(COLOR_CYAN));
      refresh();
      getch();
    }
    free(io_tail);
  }
  io_tail = NULL;
}

void io_display_tunnel(dungeon *d)
{
  uint32_t y, x;
  clear();
  for (y = 0; y < DUNGEON_Y; y++) {
    for (x = 0; x < DUNGEON_X; x++) {
      if (charxy(x, y) == d->PC) {
        mvaddch(y + 1, x, charxy(x, y)->symbol);
      } else if (hardnessxy(x, y) == 255) {
        mvaddch(y + 1, x, '*');
      } else {
        mvaddch(y + 1, x, '0' + (d->pc_tunnel[y][x] % 10));
      }
    }
  }
  refresh();
}

void io_display_distance(dungeon *d)
{
  uint32_t y, x;
  clear();
  for (y = 0; y < DUNGEON_Y; y++) {
    for (x = 0; x < DUNGEON_X; x++) {
      if (charxy(x, y)) {
        mvaddch(y + 1, x, charxy(x, y)->symbol);
      } else if (hardnessxy(x, y) != 0) {
        mvaddch(y + 1, x, ' ');
      } else {
        mvaddch(y + 1, x, '0' + (d->pc_distance[y][x] % 10));
      }
    }
  }
  refresh();
}

static char hardness_to_char[] =
  "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";

void io_display_hardness(dungeon *d)
{
  uint32_t y, x;
  clear();
  for (y = 0; y < DUNGEON_Y; y++) {
    for (x = 0; x < DUNGEON_X; x++) {
      /* Maximum hardness is 255.  We have 62 values to display it, but *
       * we only want one zero value, so we need to cover [1,255] with  *
       * 61 values, which gives us a divisor of 254 / 61 = 4.164.       *
       * Generally, we want to avoid floating point math, but this is   *
       * not gameplay, so we'll make an exception here to get maximal   *
       * hardness display resolution.                                   */
      mvaddch(y + 1, x, (d->hardness[y][x]                             ?
                         hardness_to_char[1 + (int) ((d->hardness[y][x] /
                                                      4.2))] : ' '));
    }
  }
  refresh();
}

static void io_redisplay_visible_monsters(dungeon *d)
{
  /* This was initially supposed to only redisplay visible monsters.  After *
   * implementing that (comparitivly simple) functionality and testing, I   *
   * discovered that it resulted to dead monsters being displayed beyond    *
   * their lifetimes.  So it became necessary to implement the function for *
   * everything in the light radius.  In hindsight, it would be better to   *
   * keep a static array of the things in the light radius, generated in    *
   * io_display() and referenced here to accelerate this.  The whole point  *
   * of this is to accelerate the rendering of multi-colored monsters, and  *
   * it is *significantly* faster than that (it eliminates flickering       *
   * artifacts), but it's still significantly slower than it could be.  I   *
   * will revisit this in the future to add the acceleration matrix.        */
  pair_t pos;
  uint32_t color;
  uint32_t illuminated;

  for (pos[dim_y] = -PC_VISUAL_RANGE;
       pos[dim_y] <= PC_VISUAL_RANGE;
       pos[dim_y]++) {
    for (pos[dim_x] = -PC_VISUAL_RANGE;
         pos[dim_x] <= PC_VISUAL_RANGE;
         pos[dim_x]++) {
      if ((d->PC->position[dim_y] + pos[dim_y] < 0) ||
          (d->PC->position[dim_y] + pos[dim_y] >= DUNGEON_Y) ||
          (d->PC->position[dim_x] + pos[dim_x] < 0) ||
          (d->PC->position[dim_x] + pos[dim_x] >= DUNGEON_X)) {
        continue;
      }
      if ((illuminated = is_illuminated(d->PC,
                                        d->PC->position[dim_y] + pos[dim_y],
                                        d->PC->position[dim_x] + pos[dim_x]))) {
        attron(A_BOLD);
      }
      if (d->character_map[d->PC->position[dim_y] + pos[dim_y]]
                          [d->PC->position[dim_x] + pos[dim_x]] &&
          can_see(d, d->PC->position,
                  d->character_map[d->PC->position[dim_y] + pos[dim_y]]
                                  [d->PC->position[dim_x] +
                                   pos[dim_x]]->position, 1, 0)) {
        attron(COLOR_PAIR((color = d->character_map[d->PC->position[dim_y] +
                                                    pos[dim_y]]
                                                   [d->PC->position[dim_x] +
                                                    pos[dim_x]]->get_color())));
        mvaddch(d->PC->position[dim_y] + pos[dim_y] + 1,
                d->PC->position[dim_x] + pos[dim_x],
                character_get_symbol(d->character_map[d->PC->position[dim_y] +
                                                      pos[dim_y]]
                                                     [d->PC->position[dim_x] +
                                                      pos[dim_x]]));
        attroff(COLOR_PAIR(color));
      } else if (d->objmap[d->PC->position[dim_y] + pos[dim_y]]
                          [d->PC->position[dim_x] + pos[dim_x]] &&
                 (can_see(d, d->PC->position,
                          d->objmap[d->PC->position[dim_y] + pos[dim_y]]
                                   [d->PC->position[dim_x] +
                                    pos[dim_x]]->get_position(), 1, 0) ||
                 d->objmap[d->PC->position[dim_y] + pos[dim_y]]
                          [d->PC->position[dim_x] + pos[dim_x]]->have_seen())) {
        attron(COLOR_PAIR(d->objmap[d->PC->position[dim_y] + pos[dim_y]]
                                   [d->PC->position[dim_x] +
                                    pos[dim_x]]->get_color()));
        mvaddch(d->PC->position[dim_y] + pos[dim_y] + 1,
                d->PC->position[dim_x] + pos[dim_x],
                d->objmap[d->PC->position[dim_y] + pos[dim_y]]
                         [d->PC->position[dim_x] + pos[dim_x]]->get_symbol());
        attroff(COLOR_PAIR(d->objmap[d->PC->position[dim_y] + pos[dim_y]]
                                    [d->PC->position[dim_x] +
                                     pos[dim_x]]->get_color()));
      } else {
        switch (pc_learned_terrain(d->PC,
                                   d->PC->position[dim_y] + pos[dim_y],
                                   d->PC->position[dim_x] +
                                   pos[dim_x])) {
        case ter_wall:
        case ter_wall_immutable:
        case ter_unknown:
          mvaddch(d->PC->position[dim_y] + pos[dim_y] + 1,
                  d->PC->position[dim_x] + pos[dim_x], ' ');
          break;
        case ter_floor:
        case ter_floor_room:
          mvaddch(d->PC->position[dim_y] + pos[dim_y] + 1,
                  d->PC->position[dim_x] + pos[dim_x], '.');
          break;
        case ter_floor_hall:
          mvaddch(d->PC->position[dim_y] + pos[dim_y] + 1,
                  d->PC->position[dim_x] + pos[dim_x], '#');
          break;
        case ter_debug:
          mvaddch(d->PC->position[dim_y] + pos[dim_y] + 1,
                  d->PC->position[dim_x] + pos[dim_x], '*');
          break;
        case ter_stairs_up:
          mvaddch(d->PC->position[dim_y] + pos[dim_y] + 1,
                  d->PC->position[dim_x] + pos[dim_x], '<');
          break;
        case ter_stairs_down:
          mvaddch(d->PC->position[dim_y] + pos[dim_y] + 1,
                  d->PC->position[dim_x] + pos[dim_x], '>');
          break;
        default:
 /* Use zero as an error symbol, since it stands out somewhat, and it's *
  * not otherwise used.                                                 */
          mvaddch(d->PC->position[dim_y] + pos[dim_y] + 1,
                  d->PC->position[dim_x] + pos[dim_x], '0');
        }
      }
      attroff(A_BOLD);
    }
  }

  refresh();
}

static int compare_monster_distance(const void *v1, const void *v2)
{
  const character *const *c1 = (const character *const *) v1;
  const character *const *c2 = (const character *const *) v2;

  return (thedungeon->pc_distance[(*c1)->position[dim_y]]
                                 [(*c1)->position[dim_x]] -
          thedungeon->pc_distance[(*c2)->position[dim_y]]
                                 [(*c2)->position[dim_x]]);
}

static character *io_nearest_visible_monster(dungeon *d)
{
  character **c, *n;
  uint32_t x, y, count, i;

  c = (character **) malloc(d->num_monsters * sizeof (*c));

  /* Get a linear list of monsters */
  for (count = 0, y = 1; y < DUNGEON_Y - 1; y++) {
    for (x = 1; x < DUNGEON_X - 1; x++) {
      if (d->character_map[y][x] && d->character_map[y][x] != d->PC) {
        c[count++] = d->character_map[y][x];
      }
    }
  }

  /* Sort it by distance from PC */
  thedungeon = d;
  qsort(c, count, sizeof (*c), compare_monster_distance);

  for (n = NULL, i = 0; i < count; i++) {
    if (can_see(d, character_get_pos(d->PC), character_get_pos(c[i]), 1, 0)) {
      n = c[i];
      break;
    }
  }

  free(c);

  return n;
}

void io_display(dungeon *d)
{
  pair_t pos;
  uint32_t illuminated;
  uint32_t color;
  character *c;
  int32_t visible_monsters;

  clear();
  for (visible_monsters = -1, pos[dim_y] = 0;
       pos[dim_y] < DUNGEON_Y;
       pos[dim_y]++) {
    for (pos[dim_x] = 0; pos[dim_x] < DUNGEON_X; pos[dim_x]++) {
      if ((illuminated = is_illuminated(d->PC,
                                        pos[dim_y],
                                        pos[dim_x]))) {
        attron(A_BOLD);
      }
      if (d->character_map[pos[dim_y]]
                          [pos[dim_x]] &&
          can_see(d,
                  character_get_pos(d->PC),
                  character_get_pos(d->character_map[pos[dim_y]]
                                                    [pos[dim_x]]), 1, 0)) {
        visible_monsters++;
        attron(COLOR_PAIR((color = d->character_map[pos[dim_y]]
                                                   [pos[dim_x]]->get_color())));
        mvaddch(pos[dim_y] + 1, pos[dim_x],
                character_get_symbol(d->character_map[pos[dim_y]]
                                                     [pos[dim_x]]));
        attroff(COLOR_PAIR(color));
      } else if (d->objmap[pos[dim_y]]
                          [pos[dim_x]] &&
                 (d->objmap[pos[dim_y]]
                           [pos[dim_x]]->have_seen() ||
                  can_see(d, character_get_pos(d->PC), pos, 1, 0))) {
        attron(COLOR_PAIR(d->objmap[pos[dim_y]]
                                   [pos[dim_x]]->get_color()));
        mvaddch(pos[dim_y] + 1, pos[dim_x],
                d->objmap[pos[dim_y]]
                         [pos[dim_x]]->get_symbol());
        attroff(COLOR_PAIR(d->objmap[pos[dim_y]]
                                    [pos[dim_x]]->get_color()));
      } else {
        switch (pc_learned_terrain(d->PC,
                                   pos[dim_y],
                                   pos[dim_x])) {
        case ter_wall:
        case ter_wall_immutable:
        case ter_unknown:
          mvaddch(pos[dim_y] + 1, pos[dim_x], ' ');
          break;
        case ter_floor:
        case ter_floor_room:
          mvaddch(pos[dim_y] + 1, pos[dim_x], '.');
          break;
        case ter_floor_hall:
          mvaddch(pos[dim_y] + 1, pos[dim_x], '#');
          break;
        case ter_debug:
          mvaddch(pos[dim_y] + 1, pos[dim_x], '*');
          break;
        case ter_stairs_up:
          mvaddch(pos[dim_y] + 1, pos[dim_x], '<');
          break;
        case ter_stairs_down:
          mvaddch(pos[dim_y] + 1, pos[dim_x], '>');
          break;
        default:
 /* Use zero as an error symbol, since it stands out somewhat, and it's *
  * not otherwise used.                                                 */
          mvaddch(pos[dim_y] + 1, pos[dim_x], '0');
        }
      }
      if (illuminated) {
        attroff(A_BOLD);
      }
    }
  }

  mvprintw(23, 1, "PC position is (%2d,%2d).",
           d->PC->position[dim_x], d->PC->position[dim_y]);
  mvprintw(22, 1, "%d known %s.", visible_monsters,
           visible_monsters > 1 ? "monsters" : "monster");
  mvprintw(22, 30, "Nearest visible monster: ");
  if ((c = io_nearest_visible_monster(d))) {
    attron(COLOR_PAIR(COLOR_RED));
    mvprintw(22, 55, "%c at %d %c by %d %c.",
             c->symbol,
             abs(c->position[dim_y] - d->PC->position[dim_y]),
             ((c->position[dim_y] - d->PC->position[dim_y]) <= 0 ?
              'N' : 'S'),
             abs(c->position[dim_x] - d->PC->position[dim_x]),
             ((c->position[dim_x] - d->PC->position[dim_x]) <= 0 ?
              'W' : 'E'));
    attroff(COLOR_PAIR(COLOR_RED));
  } else {
    attron(COLOR_PAIR(COLOR_BLUE));
    mvprintw(22, 55, "NONE.");
    attroff(COLOR_PAIR(COLOR_BLUE));
  }

  io_print_message_queue(0, 0);

  refresh();
}

static void io_redisplay_non_terrain(dungeon *d, pair_t cursor)
{
  /* For the wiz-mode teleport, in order to see color-changing effects. */
  pair_t pos;
  uint32_t color;
  uint32_t illuminated;

  for (pos[dim_y] = 0; pos[dim_y] < DUNGEON_Y; pos[dim_y]++) {
    for (pos[dim_x] = 0; pos[dim_x] < DUNGEON_X; pos[dim_x]++) {
      if ((illuminated = is_illuminated(d->PC,
                                        pos[dim_y],
                                        pos[dim_x]))) {
        attron(A_BOLD);
      }
      if (cursor[dim_y] == pos[dim_y] && cursor[dim_x] == pos[dim_x]) {
        mvaddch(pos[dim_y] + 1, pos[dim_x], '*');
      } else if (d->character_map[pos[dim_y]][pos[dim_x]]) {
        attron(COLOR_PAIR((color = d->character_map[pos[dim_y]]
                                                   [pos[dim_x]]->get_color())));
        mvaddch(pos[dim_y] + 1, pos[dim_x],
                character_get_symbol(d->character_map[pos[dim_y]][pos[dim_x]]));
        attroff(COLOR_PAIR(color));
      } else if (d->objmap[pos[dim_y]][pos[dim_x]]) {
        attron(COLOR_PAIR(d->objmap[pos[dim_y]][pos[dim_x]]->get_color()));
        mvaddch(pos[dim_y] + 1, pos[dim_x],
                d->objmap[pos[dim_y]][pos[dim_x]]->get_symbol());
        attroff(COLOR_PAIR(d->objmap[pos[dim_y]][pos[dim_x]]->get_color()));
      }
      attroff(A_BOLD);
    }
  }

  refresh();
}

void io_display_no_fog(dungeon *d)
{
  uint32_t y, x;
  uint32_t color;
  character *c;

  clear();
  for (y = 0; y < DUNGEON_Y; y++) {
    for (x = 0; x < DUNGEON_X; x++) {
      if (d->character_map[y][x]) {
        attron(COLOR_PAIR((color = d->character_map[y][x]->get_color())));
        mvaddch(y + 1, x, character_get_symbol(d->character_map[y][x]));
        attroff(COLOR_PAIR(color));
      } else if (d->objmap[y][x]) {
        attron(COLOR_PAIR(d->objmap[y][x]->get_color()));
        mvaddch(y + 1, x, d->objmap[y][x]->get_symbol());
        attroff(COLOR_PAIR(d->objmap[y][x]->get_color()));
      } else {
        switch (mapxy(x, y)) {
        case ter_wall:
        case ter_wall_immutable:
          mvaddch(y + 1, x, ' ');
          break;
        case ter_floor:
        case ter_floor_room:
          mvaddch(y + 1, x, '.');
          break;
        case ter_floor_hall:
          mvaddch(y + 1, x, '#');
          break;
        case ter_debug:
          mvaddch(y + 1, x, '*');
          break;
        case ter_stairs_up:
          mvaddch(y + 1, x, '<');
          break;
        case ter_stairs_down:
          mvaddch(y + 1, x, '>');
          break;
        default:
 /* Use zero as an error symbol, since it stands out somewhat, and it's *
  * not otherwise used.                                                 */
          mvaddch(y + 1, x, '0');
        }
      }
    }
  }

  mvprintw(23, 1, "PC position is (%2d,%2d).",
           d->PC->position[dim_x], d->PC->position[dim_y]);
  mvprintw(22, 1, "%d %s.", d->num_monsters,
           d->num_monsters > 1 ? "monsters" : "monster");
  mvprintw(22, 30, "Nearest visible monster: ");
  if ((c = io_nearest_visible_monster(d))) {
    attron(COLOR_PAIR(COLOR_RED));
    mvprintw(22, 55, "%c at %d %c by %d %c.",
             c->symbol,
             abs(c->position[dim_y] - d->PC->position[dim_y]),
             ((c->position[dim_y] - d->PC->position[dim_y]) <= 0 ?
              'N' : 'S'),
             abs(c->position[dim_x] - d->PC->position[dim_x]),
             ((c->position[dim_x] - d->PC->position[dim_x]) <= 0 ?
              'W' : 'E'));
    attroff(COLOR_PAIR(COLOR_RED));
  } else {
    attron(COLOR_PAIR(COLOR_BLUE));
    mvprintw(22, 55, "NONE.");
    attroff(COLOR_PAIR(COLOR_BLUE));
  }

  io_print_message_queue(0, 0);

  refresh();
}

void io_display_monster_list(dungeon *d)
{
  mvprintw(11, 33, " HP:    XXXXX ");
  mvprintw(12, 33, " Speed: XXXXX ");
  mvprintw(14, 27, " Hit any key to continue. ");
  refresh();
  getch();
}

uint32_t io_teleport_pc(dungeon *d)
{
  pair_t dest;
  int c;
  fd_set readfs;
  struct timeval tv;

  pc_reset_visibility(d->PC);
  io_display_no_fog(d);

  mvprintw(0, 0,
           "Choose a location.  'g' or '.' to teleport to; 'r' for random.");

  dest[dim_y] = d->PC->position[dim_y];
  dest[dim_x] = d->PC->position[dim_x];

  mvaddch(dest[dim_y] + 1, dest[dim_x], '*');
  refresh();

  do {
    do{
      FD_ZERO(&readfs);
      FD_SET(STDIN_FILENO, &readfs);

      tv.tv_sec = 0;
      tv.tv_usec = 125000; /* An eigth of a second */

      io_redisplay_non_terrain(d, dest);
    } while (!select(STDIN_FILENO + 1, &readfs, NULL, NULL, &tv));
    /* Can simply draw the terrain when we move the cursor away, *
     * because if it is a character or object, the refresh       *
     * function will fix it for us.                              */
    switch (mappair(dest)) {
    case ter_wall:
    case ter_wall_immutable:
    case ter_unknown:
      mvaddch(dest[dim_y] + 1, dest[dim_x], ' ');
      break;
    case ter_floor:
    case ter_floor_room:
      mvaddch(dest[dim_y] + 1, dest[dim_x], '.');
      break;
    case ter_floor_hall:
      mvaddch(dest[dim_y] + 1, dest[dim_x], '#');
      break;
    case ter_debug:
      mvaddch(dest[dim_y] + 1, dest[dim_x], '*');
      break;
    case ter_stairs_up:
      mvaddch(dest[dim_y] + 1, dest[dim_x], '<');
      break;
    case ter_stairs_down:
      mvaddch(dest[dim_y] + 1, dest[dim_x], '>');
      break;
    default:
 /* Use zero as an error symbol, since it stands out somewhat, and it's *
  * not otherwise used.                                                 */
      mvaddch(dest[dim_y] + 1, dest[dim_x], '0');
    }
    switch ((c = getch())) {
    case '7':
    case 'y':
    case KEY_HOME:
      if (dest[dim_y] != 1) {
        dest[dim_y]--;
      }
      if (dest[dim_x] != 1) {
        dest[dim_x]--;
      }
      break;
    case '8':
    case 'k':
    case KEY_UP:
      if (dest[dim_y] != 1) {
        dest[dim_y]--;
      }
      break;
    case '9':
    case 'u':
    case KEY_PPAGE:
      if (dest[dim_y] != 1) {
        dest[dim_y]--;
      }
      if (dest[dim_x] != DUNGEON_X - 2) {
        dest[dim_x]++;
      }
      break;
    case '6':
    case 'l':
    case KEY_RIGHT:
      if (dest[dim_x] != DUNGEON_X - 2) {
        dest[dim_x]++;
      }
      break;
    case '3':
    case 'n':
    case KEY_NPAGE:
      if (dest[dim_y] != DUNGEON_Y - 2) {
        dest[dim_y]++;
      }
      if (dest[dim_x] != DUNGEON_X - 2) {
        dest[dim_x]++;
      }
      break;
    case '2':
    case 'j':
    case KEY_DOWN:
      if (dest[dim_y] != DUNGEON_Y - 2) {
        dest[dim_y]++;
      }
      break;
    case '1':
    case 'b':
    case KEY_END:
      if (dest[dim_y] != DUNGEON_Y - 2) {
        dest[dim_y]++;
      }
      if (dest[dim_x] != 1) {
        dest[dim_x]--;
      }
      break;
    case '4':
    case 'h':
    case KEY_LEFT:
      if (dest[dim_x] != 1) {
        dest[dim_x]--;
      }
      break;
    }
  } while (c != 'g' && c != '.' && c != 'r');

  if (c == 'r') {
    do {
      dest[dim_x] = rand_range(1, DUNGEON_X - 2);
      dest[dim_y] = rand_range(1, DUNGEON_Y - 2);
    } while (charpair(dest) || mappair(dest) < ter_floor);
  }

  if (charpair(dest) && charpair(dest) != d->PC) {
    io_queue_message("Teleport failed.  Destination occupied.");
  } else {  
    d->character_map[d->PC->position[dim_y]][d->PC->position[dim_x]] = NULL;
    d->character_map[dest[dim_y]][dest[dim_x]] = d->PC;

    d->PC->position[dim_y] = dest[dim_y];
    d->PC->position[dim_x] = dest[dim_x];
  }

  pc_observe_terrain(d->PC, d);
  dijkstra(d);
  dijkstra_tunnel(d);

  io_display(d);

  return 0;
}

/* Adjectives to describe our monsters */
static const char *adjectives[] = {
  "A menacing ",
  "A threatening ",
  "A horrifying ",
  "An intimidating ",
  "An aggressive ",
  "A frightening ",
  "A terrifying ",
  "A terrorizing ",
  "An alarming ",
  "A dangerous ",
  "A glowering ",
  "A glaring ",
  "A scowling ",
  "A chilling ",
  "A scary ",
  "A creepy ",
  "An eerie ",
  "A spooky ",
  "A slobbering ",
  "A drooling ",
  "A horrendous ",
  "An unnerving ",
  "A cute little ",  /* Even though they're trying to kill you, */
  "A teeny-weenie ", /* they can still be cute!                 */
  "A fuzzy ",
  "A fluffy white ",
  "A kawaii ",       /* For our otaku */
  "Hao ke ai de ",   /* And for our Chinese */
  "Eine liebliche "  /* For our Deutch */
  /* And there's one special case (see below) */
};

static void io_scroll_monster_list(char (*s)[60], uint32_t count)
{
  uint32_t offset;
  uint32_t i;

  offset = 0;

  while (1) {
    for (i = 0; i < 13; i++) {
      mvprintw(i + 6, 9, " %-60s ", s[i + offset]);
    }
    switch (getch()) {
    case KEY_UP:
      if (offset) {
        offset--;
      }
      break;
    case KEY_DOWN:
      if (offset < (count - 13)) {
        offset++;
      }
      break;
    case 27:
      return;
    }

  }
}

static bool is_vowel(const char c)
{
  return (c == 'a' || c == 'e' || c == 'i' || c == 'o' || c == 'u' ||
          c == 'A' || c == 'E' || c == 'I' || c == 'O' || c == 'U');
}

static void io_list_monsters_display(dungeon *d,
                                     character **c,
                                     uint32_t count)
{
  uint32_t i;
  char (*s)[60]; /* pointer to array of 60 char */
  char tmp[41];  /* 19 bytes for relative direction leaves 40 bytes *
                  * for the monster's name (and one for null).      */

  (void) adjectives;

  s = (char (*)[60]) malloc((count + 1) * sizeof (*s));

  mvprintw(3, 9, " %-60s ", "");
  /* Borrow the first element of our array for this string: */
  snprintf(s[0], 60, "You know of %d monsters:", count);
  mvprintw(4, 9, " %-60s ", s);
  mvprintw(5, 9, " %-60s ", "");

  for (i = 0; i < count; i++) {
    snprintf(tmp, 41, "%3s%s (%c): ",
             (is_unique(c[i]) ? "" :
              (is_vowel(character_get_name(c[i])[0]) ? "An " : "A ")),
             character_get_name(c[i]),
             character_get_symbol(c[i]));
    /* These pragma's suppress a "format truncation" warning from gcc. *
     * Stumbled upon a GCC bug when updating monster lists for 1.08.   *
     * Bug is known:                                                   *
     *    https://gcc.gnu.org/bugzilla/show_bug.cgi?id=78969           *
     * GCC calculates a maximum length for the output string under the *
     * assumption that the int conversions can be 11 digits long (-2.1 *
     * billion).  The ints below can never be more than 2 digits.      *
     * Tried supressing the warning by taking the ints mod 100, but    *
     * GCC wasn't smart enough for that, so using a pragma instead.    */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat"
    snprintf(s[i], 60, "%40s%2d %s by %2d %s", tmp,
             abs(character_get_y(c[i]) - character_get_y(d->PC)),
             ((character_get_y(c[i]) - character_get_y(d->PC)) <= 0 ?
              "North" : "South"),
             abs(character_get_x(c[i]) - character_get_x(d->PC)),
             ((character_get_x(c[i]) - character_get_x(d->PC)) <= 0 ?
              "West" : "East"));
#pragma GCC diagnostic pop
    if (count <= 13) {
      /* Handle the non-scrolling case right here. *
       * Scrolling in another function.            */
      mvprintw(i + 6, 9, " %-60s ", s[i]);
    }
  }

  if (count <= 13) {
    mvprintw(count + 6, 9, " %-60s ", "");
    mvprintw(count + 7, 9, " %-60s ", "Hit escape to continue.");
    while (getch() != 27 /* escape */)
      ;
  } else {
    mvprintw(19, 9, " %-60s ", "");
    mvprintw(20, 9, " %-60s ",
             "Arrows to scroll, escape to continue.");
    io_scroll_monster_list(s, count);
  }

  free(s);
}

static void io_list_monsters(dungeon *d)
{
  character **c;
  uint32_t x, y, count;

  c = (character **) malloc(d->num_monsters * sizeof (*c));

  /* Get a linear list of monsters */
  for (count = 0, y = 1; y < DUNGEON_Y - 1; y++) {
    for (x = 1; x < DUNGEON_X - 1; x++) {
      if (d->character_map[y][x] && d->character_map[y][x] != d->PC &&
          can_see(d, character_get_pos(d->PC),
                  character_get_pos(d->character_map[y][x]), 1, 0)) {
        c[count++] = d->character_map[y][x];
      }
    }
  }

  /* Sort it by distance from PC */
  thedungeon = d;
  qsort(c, count, sizeof (*c), compare_monster_distance);

  /* Display it */
  io_list_monsters_display(d, c, count);
  free(c);

  /* And redraw the dungeon */
  io_display(d);
}

void io_handle_input(dungeon *d)
{
  uint32_t fail_code;
  int key;
  fd_set readfs;
  struct timeval tv;
  uint32_t fog_off = 0;
  pair_t tmp = { DUNGEON_X, DUNGEON_Y };

  do {
    do{
      FD_ZERO(&readfs);
      FD_SET(STDIN_FILENO, &readfs);

      tv.tv_sec = 0;
      tv.tv_usec = 125000; /* An eigth of a second */

      if (fog_off) {
        /* Out-of-bounds cursor will not be rendered. */
        io_redisplay_non_terrain(d, tmp);
      } else {
        io_redisplay_visible_monsters(d);
      }
    } while (!select(STDIN_FILENO + 1, &readfs, NULL, NULL, &tv));
    fog_off = 0;
    switch (key = getch()) {
    case '7':
    case 'y':
    case KEY_HOME:
      fail_code = move_pc(d, 7);
      break;
    case '8':
    case 'k':
    case KEY_UP:
      fail_code = move_pc(d, 8);
      break;
    case '9':
    case 'u':
    case KEY_PPAGE:
      fail_code = move_pc(d, 9);
      break;
    case '6':
    case 'l':
    case KEY_RIGHT:
      fail_code = move_pc(d, 6);
      break;
    case '3':
    case 'n':
    case KEY_NPAGE:
      fail_code = move_pc(d, 3);
      break;
    case '2':
    case 'j':
    case KEY_DOWN:
      fail_code = move_pc(d, 2);
      break;
    case '1':
    case 'b':
    case KEY_END:
      fail_code = move_pc(d, 1);
      break;
    case '4':
    case 'h':
    case KEY_LEFT:
      fail_code = move_pc(d, 4);
      break;
    case '5':
    case ' ':
    case '.':
    case KEY_B2:
      fail_code = 0;
      break;
    case '>':
      fail_code = move_pc(d, '>');
      break;
    case '<':
      fail_code = move_pc(d, '<');
      break;
    case 'Q':
      d->quit = 1;
      fail_code = 0;
      break;
    case 'T':
      /* New command.  Display the distances for tunnelers.             */
      io_display_tunnel(d);
      fail_code = 1;
      break;
    case 'D':
      /* New command.  Display the distances for non-tunnelers.         */
      io_display_distance(d);
      fail_code = 1;
      break;
    case 'H':
      /* New command.  Display the hardnesses.                          */
      io_display_hardness(d);
      fail_code = 1;
      break;
    case 's':
      /* New command.  Return to normal display after displaying some   *
       * special screen.                                                */
      io_display(d);
      fail_code = 1;
      break;
    case 'g':
      /* Teleport the PC to a random place in the dungeon.              */
      io_teleport_pc(d);
      fail_code = 1;
      break;
    case 'f':
      io_display_no_fog(d);
      fail_code = 1;
      break;
    case 'm':
      io_list_monsters(d);
      fail_code = 1;
      break;
    case 'q':
      /* Demonstrate use of the message queue.  You can use this for *
       * printf()-style debugging (though gdb is probably a better   *
       * option.  Not that it matterrs, but using this command will  *
       * waste a turn.  Set fail_code to 1 and you should be able to *
       * figure out why I did it that way.                           */
      io_queue_message("This is the first message.");
      io_queue_message("Since there are multiple messages, "
                       "you will see \"more\" prompts.");
      io_queue_message("You can use any key to advance through messages.");
      io_queue_message("Normal gameplay will not resume until the queue "
                       "is empty.");
      io_queue_message("Long lines will be truncated, not wrapped.");
      io_queue_message("io_queue_message() is variadic and handles "
                       "all printf() conversion specifiers.");
      io_queue_message("Did you see %s?", "what I did there");
      io_queue_message("When the last message is displayed, there will "
                       "be no \"more\" prompt.");
      io_queue_message("Have fun!  And happy printing!");
      fail_code = 0;
      break;
    case 'w':
      {
	equip_item(d);
	fail_code =0;
      }
      break;
    case 'e':
      {
	io_list_equipment(d);
	fail_code =0;
      }
      break;
    case 'L':
      {
	monster_examine(d);
	fail_code =0;
      }
      break;
    case 'I':
      {
	inspect_item(d);
	fail_code =0;
      }
      break;
    case 't':
      {
	remove_item(d);
	fail_code =0;
      }
      break;
    case 'v':
      {
	vend_items(d);
	fail_code =0;
      }
      break;
    case 'x':
      {
	delete_item(d);
	fail_code =0;
      }
      break;
    case 'd':
      {
	drop_item(d);
	fail_code =0;
      }
      break;
    case 'S':
      {
	io_list_stats(d);
	fail_code =0;
      }
      break;
    case 'i':
      {
	io_list_inventory(d);
	fail_code =0;
      }
      break;
    case 'p': 
      {
      uint32_t pcy = d->PC->position[dim_y];
      uint32_t pcx = d->PC->position[dim_x];
      
      if(d->objmap[pcy][pcx] == 0)
	{
	  io_queue_message("There's nothing here to pick up");
	}
      else
	{
	  if(d->objmap[pcy][pcx]->get_type() == object_type_t{objtype_GOLD})
	    {
	      d->PC->gp = d->PC->gp + d->objmap[pcy][pcx]->value;
	      object* db = d->objmap[pcy][pcx];
	      d->objmap[pcy][pcx] = db->next;
	      io_queue_message("Picking up item... %s", db->get_name() );
	    }
	  else
	    {
	  if(d->PC->inventory.size() >= 10)
	    {
	      io_queue_message("Your Inventory is full!");
	    }
	  else
	    {
	      //d->PC->inventory.push_back(d->objmap[pcy][pcx]);
	      //io_queue_message("Picking up item... %s", d->objmap[pcy][pcx]->get_name() );
	      //d->objmap[pcy][pcx] = 0;

	      //Pick Up
	      object* db = d->objmap[pcy][pcx];
	      d->objmap[pcy][pcx] = db->next;
	      db->next = NULL;
	      d->PC->inventory.push_back(db);
	      io_queue_message("Picking up item... %s", db->get_name() );

	    }
	    }
	}
      fail_code=0;
      }
      break;

    default:
      /* Also not in the spec.  It's not always easy to figure out what *
       * key code corresponds with a given keystroke.  Print out any    *
       * unhandled key here.  Not only does it give a visual error      *
       * indicator, but it also gives an integer value that can be used *
       * for that key in this (or other) switch statements.  Printed in *
       * octal, with the leading zero, because ncurses.h lists codes in *
       * octal, thus allowing us to do reverse lookups.  If a key has a *
       * name defined in the header, you can use the name here, else    *
       * you can directly use the octal value.                          */
      mvprintw(0, 0, "Unbound key: %#o ", key);
      fail_code = 1;
    }
  } while (fail_code);
}






void io_list_stats(dungeon *d)
{ 
  WINDOW* subwindow = newwin(18,47,0,5);

  refresh();

  box(subwindow,0,0);
 
  mvwprintw(subwindow, 14, 1, "Enter the any key to close."); 
  mvwprintw(subwindow, 1, 1, "STATS--");
  
 
    
  //mvwprintw(subwindow,pos+ii,1,"(%d): ",count );
  //  mvwprintw(subwindow, pos+ii, 6,  d->PC->inventory[ii]->get_name() ); 

  
    int32_t sumspeed=0;
    int y=0;
    for(y=0;y<12;y++)
      {
	if(d->PC->equipment[y] != NULL)
	  {
	    int32_t why =  d->PC->equipment[y]->speed;
	    sumspeed  =sumspeed +why;
	  }
	
      }
    sumspeed = sumspeed + PC_SPEED;
    if(d->PC->potspeed ==1)
      {
	sumspeed=sumspeed+100;
      }
    

  


    
  int32_t sumatk=0;
  int yy=0;
  for(yy=0;yy<12;yy++)
    {
      if(d->PC->equipment[yy] != NULL)
	    {
	      int32_t why =  d->PC->equipment[yy]->damage.roll();
	      sumatk  =sumatk +why;
	    }
      
    }
  sumatk = sumatk + d->PC->damage->roll();
  if(d->PC->potdamage ==1)
    {
      sumatk = sumatk+150;
    }


  mvwprintw(subwindow,3,1,"(HP)" );
  std::string hs = ": "+std::to_string(d->PC->hp);
  char const *hchar = hs.c_str();
  mvwprintw(subwindow,3,10,hchar);

  mvwprintw(subwindow,4,1,"(Damage)" );
  std::string ds = ": "+std::to_string(sumatk);
  char const *dchar = ds.c_str();
  mvwprintw(subwindow,4,10,dchar);

  mvwprintw(subwindow,5,1,"(Speed)" );
  std::string ss = ": "+std::to_string(sumspeed);
  char const *schar = ss.c_str();
  mvwprintw(subwindow,5,10,schar);

  mvwprintw(subwindow,6,1,"(Gold)" );
  std::string gs = ": "+std::to_string(d->PC->gp);
  char const *gchar = gs.c_str();
  mvwprintw(subwindow,6,10,gchar);

  mvwprintw(subwindow,7,1,"(Name)" );
  char nbuffer[300];
  const char* nword1 =  d->PC->name;
  const char* nword2 = ": ";
  strncpy(nbuffer, nword2, sizeof(nbuffer));
  strncat(nbuffer, nword1, sizeof(nbuffer));
  mvwprintw(subwindow,7,10,nbuffer);

  wrefresh(subwindow);
  int input = getch();
  
  if(input == 27)
    {
      delwin(subwindow);
    }
}





void monster_examine(dungeon *d)
{
  pair_t dest;
  int c;
  fd_set readfs;
  struct timeval tv;

  pc_reset_visibility(d->PC);
  io_display_no_fog(d);

  mvprintw(0, 0,
           "Choose a location. using 'g'");

  dest[dim_y] = d->PC->position[dim_y];
  dest[dim_x] = d->PC->position[dim_x];

  mvaddch(dest[dim_y] + 1, dest[dim_x], '*');
  refresh();

  do {
    do{
      FD_ZERO(&readfs);
      FD_SET(STDIN_FILENO, &readfs);

      tv.tv_sec = 0;
      tv.tv_usec = 125000; /* An eigth of a second */

      io_redisplay_non_terrain(d, dest);
    } while (!select(STDIN_FILENO + 1, &readfs, NULL, NULL, &tv));
    /* Can simply draw the terrain when we move the cursor away, *
     * because if it is a character or object, the refresh       *
     * function will fix it for us.                              */
    switch (mappair(dest)) {
    case ter_wall:
    case ter_wall_immutable:
    case ter_unknown:
      mvaddch(dest[dim_y] + 1, dest[dim_x], ' ');
      break;
    case ter_floor:
    case ter_floor_room:
      mvaddch(dest[dim_y] + 1, dest[dim_x], '.');
      break;
    case ter_floor_hall:
      mvaddch(dest[dim_y] + 1, dest[dim_x], '#');
      break;
    case ter_debug:
      mvaddch(dest[dim_y] + 1, dest[dim_x], '*');
      break;
    case ter_stairs_up:
      mvaddch(dest[dim_y] + 1, dest[dim_x], '<');
      break;
    case ter_stairs_down:
      mvaddch(dest[dim_y] + 1, dest[dim_x], '>');
      break;
    default:
 /* Use zero as an error symbol, since it stands out somewhat, and it's *
  * not otherwise used.                                                 */
      mvaddch(dest[dim_y] + 1, dest[dim_x], '0');
    }
    switch ((c = getch())) {
    case '7':
    case 'y':
    case KEY_HOME:
      if (dest[dim_y] != 1) {
        dest[dim_y]--;
      }
      if (dest[dim_x] != 1) {
        dest[dim_x]--;
      }
      break;
    case '8':
    case 'k':
    case KEY_UP:
      if (dest[dim_y] != 1) {
        dest[dim_y]--;
      }
      break;
    case '9':
    case 'u':
    case KEY_PPAGE:
      if (dest[dim_y] != 1) {
        dest[dim_y]--;
      }
      if (dest[dim_x] != DUNGEON_X - 2) {
        dest[dim_x]++;
      }
      break;
    case '6':
    case 'l':
    case KEY_RIGHT:
      if (dest[dim_x] != DUNGEON_X - 2) {
        dest[dim_x]++;
      }
      break;
    case '3':
    case 'n':
    case KEY_NPAGE:
      if (dest[dim_y] != DUNGEON_Y - 2) {
        dest[dim_y]++;
      }
      if (dest[dim_x] != DUNGEON_X - 2) {
        dest[dim_x]++;
      }
      break;
    case '2':
    case 'j':
    case KEY_DOWN:
      if (dest[dim_y] != DUNGEON_Y - 2) {
        dest[dim_y]++;
      }
      break;
    case '1':
    case 'b':
    case KEY_END:
      if (dest[dim_y] != DUNGEON_Y - 2) {
        dest[dim_y]++;
      }
      if (dest[dim_x] != 1) {
        dest[dim_x]--;
      }
      break;
    case '4':
    case 'h':
    case KEY_LEFT:
      if (dest[dim_x] != 1) {
        dest[dim_x]--;
      }
      break;
    }
  } while (c != 'g');

  pc_observe_terrain(d->PC, d);
  dijkstra(d);
  dijkstra_tunnel(d);

  io_display(d);
  if ((charpair(dest) && charpair(dest) != d->PC)) 
    {
     
      WINDOW* subwindow = newwin(22,80,0,5);
      
      refresh();
      
      box(subwindow,0,0);
      
      mvwprintw(subwindow, 14, 1, "Enter the any key to close."); 
      mvwprintw(subwindow, 1, 1, "Monster Description--");
     
      character *charr = d->character_map[dest[dim_y]][dest[dim_x]];
      npc *ttempp = (npc *) charr;
      
      mvwprintw(subwindow, 4, 1, charr->name );
      mvwprintw(subwindow, 5, 1, ttempp->description ); 
    
	

    wrefresh(subwindow);
  int input = getch();

  if(input == 27)
    {
      delwin(subwindow);
    }
 
    }
  else
    {
      io_queue_message("There is no monster in this position.");
    }
 
}



void io_list_equipment(dungeon *d)
{
  
  WINDOW* subwindow = newwin(18,50,0,5);

  refresh();

  box(subwindow,0,0);
 
  mvwprintw(subwindow, 14, 1, "Enter the any key to close."); 
  mvwprintw(subwindow, 1, 1, "PC Equipment--");

  uint32_t ii =0;
  uint32_t pos =2;
  int count =0;
  for(ii =0;ii< sizeof(d->PC->equipment)/sizeof(d->PC->equipment[0]);ii++)
    {
    
            if(count ==0)
	{
	  mvwprintw(subwindow,pos+ii,6,"Weapon :",count );
	}
      else if( count ==1)
	{
	  mvwprintw(subwindow,pos+ii,6,"Offhand:",count );
	}
      else if( count ==2)
	{
	  mvwprintw(subwindow,pos+ii,6,"Ranged :",count );
	}
      else if( count ==3)
	{
	  mvwprintw(subwindow,pos+ii,6,"Armor  :",count );
	}
      else if( count ==4)
	{
	  mvwprintw(subwindow,pos+ii,6,"Helmet :",count );
	}
      else if( count ==5)
	{
	  mvwprintw(subwindow,pos+ii,6,"Cloak  :",count );
	}
      else if( count ==6)
	{
	  mvwprintw(subwindow,pos+ii,6,"Gloves :",count );
	}
      else if( count ==7)
	{
	  mvwprintw(subwindow,pos+ii,6,"Boots  :",count );
	}
      else if( count ==8)
	{
	  mvwprintw(subwindow,pos+ii,6,"Amulet :",count );
	}
      else if( count ==9)
	{
	  mvwprintw(subwindow,pos+ii,6,"Light  :",count );
	}
      else if( count ==10)
	{
	  mvwprintw(subwindow,pos+ii,6,"Ring1  :",count );
	}
      else if( count ==11)
	{
	  mvwprintw(subwindow,pos+ii,6,"Ring2  :",count );
	}
	    if(count == 10)
	      {
		mvwprintw(subwindow,pos+ii,1,"(a): ");
	      }
	    else if(count ==11)
	      {
		mvwprintw(subwindow,pos+ii,1,"(b): ");
	      }
	    else
	      {
		mvwprintw(subwindow,pos+ii,1,"(%d): ",count );
	      }
      if(d->PC->equipment[ii] != NULL)
	{
	  mvwprintw(subwindow, pos+ii, 16,  d->PC->equipment[ii]->get_name() ); 
	}
      count = count +1;
    }

    wrefresh(subwindow);


  int input = getch();

  if(input == 27)
    {
      delwin(subwindow);
    }
}

void io_list_inventory(dungeon *d)
{ 
  WINDOW* subwindow = newwin(18,47,0,5);

  refresh();

  box(subwindow,0,0);
 
  mvwprintw(subwindow, 14, 1, "Enter the any key to close."); 
  mvwprintw(subwindow, 1, 1, "INVENTORY--");
  uint32_t ii =0;
  uint32_t pos =2;
  int count =0;
  for(ii =0;ii< d->PC->inventory.size();ii++)
    {
    
      mvwprintw(subwindow,pos+ii,1,"(%d): ",count );
      mvwprintw(subwindow, pos+ii, 6,  d->PC->inventory[ii]->get_name() ); 
      count = count +1;
    }

    wrefresh(subwindow);
  int input = getch();

  if(input == 27)
    {
      delwin(subwindow);
    }
}

void vend_items(dungeon *d)
{ WINDOW* subwindow = newwin(22,80,0,5);

  refresh();

  box(subwindow,0,0);
 
  mvwprintw(subwindow, 14, 1, "Enter the item you want to purchase."); 
  mvwprintw(subwindow, 1, 1, "Grand Exchange--");

  std::vector<object*> ovec;
  object *o;
  std::vector<object_description> &v = d->object_descriptions;
  uint32_t i;
  character *charr = d->PC;
    
  for(i=0;i<v.size();i++)
    {
      if(v[i].type == object_type_t{objtype_FLASK})
	{
	  o = new object(v[i], charr->position, d->objmap[charr->position[dim_y]][charr->position[dim_x]]);
	  ovec.push_back(o);
	}
      else if(v[i].name.compare("ShaefferGodsword")==0)
	{
	  o = new object(v[i], charr->position, d->objmap[charr->position[dim_y]][charr->position[dim_x]]);
	  ovec.push_back(o);
	}
    }
 

  uint32_t ii =0;
  uint32_t pos =2;
  int count =0;
  for(ii =0;ii< ovec.size();ii++)
    {
    
      mvwprintw(subwindow,pos+ii,1,"(%d): ",count );
      mvwprintw(subwindow, pos+ii, 6,  ovec[ii]->get_name() ); 
      mvwprintw(subwindow, pos+ii, 30,  "Cost : %d gp",ovec[ii]->value ); 
      count = count +1;
    }

    wrefresh(subwindow);



  int input = getch();
  int temp = (int)((char)input - '0');

      if(temp >=0 && (uint32_t) temp < ovec.size())
	{
	  if(d->PC->inventory.size() <10)
	    {
	      if(d->PC->gp > ovec[temp]->value )
		{	  
		  d->PC->inventory.push_back(ovec[temp]);
		  d->PC->gp = d->PC->gp - ovec[temp]->value;
		}
	      else
		{
		  io_queue_message("You don't have enough gp to buy that!");
		}
	    }
	  else
	    {
	      io_queue_message("You don't have any space for that item");
	    }
	}
      else
	{
	  io_queue_message("That item does not exist");
	}

      
  
}





void drop_item(dungeon *d)
{
 WINDOW* subwindow = newwin(22,80,0,5);

  refresh();

  box(subwindow,0,0);
 
  mvwprintw(subwindow, 14, 1, "Enter the item you want to drop"); 
  mvwprintw(subwindow, 1, 1, "DROP INVENTORY--");
  uint32_t ii =0;
  uint32_t pos =2;
  int count =0;
  for(ii =0;ii< d->PC->inventory.size();ii++)
    {
    
      mvwprintw(subwindow,pos+ii,1,"(%d): ",count );
      mvwprintw(subwindow, pos+ii, 6,  d->PC->inventory[ii]->get_name() ); 
      count = count +1;
    }

    wrefresh(subwindow);
  int input = getch();
  int temp = (int)((char)input - '0');

      if(temp >=0 && (uint32_t) temp < d->PC->inventory.size())
	{
	  uint32_t pcy = d->PC->position[dim_y];
	  uint32_t pcx = d->PC->position[dim_x];
	  
	  //drop
	  object* db = d->PC->inventory[temp];
	  object* bd = d->objmap[pcy][pcx];
	  db->next = bd;
	  d->objmap[pcy][pcx] = db;
	  d->PC->inventory.erase(d->PC->inventory.begin() + temp );
	  io_queue_message("You drop the item... %s", db->get_name() );
	}
      else
	{
	  io_queue_message("That item does not exist.");
	}
      
}

void inspect_item(dungeon *d)
{
 WINDOW* subwindow = newwin(22,80,0,5);

  refresh();

  box(subwindow,0,0);
 
  mvwprintw(subwindow, 14, 1, "Enter the item you want to Inspect/Press ESC to close"); 
  mvwprintw(subwindow, 1, 1, "INSPECT INVENTORY--");
  uint32_t ii =0;
  uint32_t pos =2;
  int count =0;
  for(ii =0;ii< d->PC->inventory.size();ii++)
    {
    
      mvwprintw(subwindow,pos+ii,1,"(%d): ",count );
      mvwprintw(subwindow, pos+ii, 6,  d->PC->inventory[ii]->get_name() ); 
      count = count +1;
    }

    wrefresh(subwindow);
  int input = getch();
  int temp = (int)((char)input - '0');

      if(temp >=0 && (uint32_t) temp < d->PC->inventory.size())
	{
	  mvwprintw(subwindow,16,1,d->PC->inventory[temp]->get_desc());
	  wrefresh(subwindow);
	}
      else
	{
	   mvwprintw(subwindow,16,1,"The item you entered does not exist.");
	   wrefresh(subwindow);
	}
      int newinput= getch();
      if(newinput == 27)
	{
	  delwin(subwindow);
	}

}

void delete_item(dungeon *d)
{
 WINDOW* subwindow = newwin(18,47,0,5);

  refresh();

  box(subwindow,0,0);
 
  mvwprintw(subwindow, 14, 1, "Enter the item you want to Expunge."); 
  mvwprintw(subwindow, 1, 1, "INVENTORY--");
  uint32_t ii =0;
  uint32_t pos =2;
  int count =0;
  for(ii =0;ii< d->PC->inventory.size();ii++)
    {
    
      mvwprintw(subwindow,pos+ii,1,"(%d): ",count );
      mvwprintw(subwindow, pos+ii, 6,  d->PC->inventory[ii]->get_name() ); 
      count = count +1;
    }

    wrefresh(subwindow);
  int input = getch();
  int temp = (int)((char)input - '0');
  if(input == 27)
    {
      delwin(subwindow);
    }
  else
    {
      if(temp >=0 && (uint32_t) temp < d->PC->inventory.size())
	{
	  const char* word1 =  d->PC->inventory[temp]->get_name();
	  d->PC->inventory.erase(d->PC->inventory.begin() + temp );

	 
	  const char* word2 = "You have successfully destroyed the item: ";
	  
	  char buffer[300]; 
	  strncpy(buffer, word2, sizeof(buffer));
	  strncat(buffer, word1, sizeof(buffer));

	  io_queue_message(buffer);
	}
      else
	{
	  io_queue_message("The Item you entered does not exist.");
	}
    }

}


void remove_item(dungeon *d)
{
  
  WINDOW* subwindow = newwin(18,50,0,5);

  refresh();

  box(subwindow,0,0);
 
  mvwprintw(subwindow, 14, 1, "Enter the index of the item you want to remove");
  mvwprintw(subwindow, 1, 1, "PC Equipment--");

  uint32_t ii =0;
  uint32_t pos =2;
  int count =0;
  for(ii =0;ii< sizeof(d->PC->equipment)/sizeof(d->PC->equipment[0]);ii++)
    {
   
            if(count ==0)
	{
	  mvwprintw(subwindow,pos+ii,6,"Weapon :",count );
	}
      else if( count ==1)
	{
	  mvwprintw(subwindow,pos+ii,6,"Offhand:",count );
	}
      else if( count ==2)
	{
	  mvwprintw(subwindow,pos+ii,6,"Ranged :",count );
	}
      else if( count ==3)
	{
	  mvwprintw(subwindow,pos+ii,6,"Armor  :",count );
	}
      else if( count ==4)
	{
	  mvwprintw(subwindow,pos+ii,6,"Helmet :",count );
	}
      else if( count ==5)
	{
	  mvwprintw(subwindow,pos+ii,6,"Cloak  :",count );
	}
      else if( count ==6)
	{
	  mvwprintw(subwindow,pos+ii,6,"Gloves :",count );
	}
      else if( count ==7)
	{
	  mvwprintw(subwindow,pos+ii,6,"Boots  :",count );
	}
      else if( count ==8)
	{
	  mvwprintw(subwindow,pos+ii,6,"Amulet :",count );
	}
      else if( count ==9)
	{
	  mvwprintw(subwindow,pos+ii,6,"Light  :",count );
	}
      else if( count ==10)
	{
	  mvwprintw(subwindow,pos+ii,6,"Ring1  :",count );
	}
      else if( count ==11)
	{
	  mvwprintw(subwindow,pos+ii,6,"Ring2  :",count );
	}
	    
	    if(count == 10)
	      {
		mvwprintw(subwindow,pos+ii,1,"(a): ");
	      }
	    else if(count ==11)
	      {
		mvwprintw(subwindow,pos+ii,1,"(b): ");
	      }
	    else
	      {
		mvwprintw(subwindow,pos+ii,1,"(%d): ",count );
	      }

      if(d->PC->equipment[ii] != NULL)
	{
	  mvwprintw(subwindow, pos+ii, 16,  d->PC->equipment[ii]->get_name() ); 
	}
      count = count +1;
    }

    wrefresh(subwindow);
  int input = getch();
  int temp = (int)((char)input - '0');
  
  if(input == 'a')
    {
      temp = 10;
    }
  else if(input =='b')
    {
      temp = 11;
    }

    if(input == 27)
      {
	 delwin(subwindow);
      }
    else
      {
	if( (temp >=0 && (uint32_t)temp <  sizeof(d->PC->equipment)/sizeof(d->PC->equipment[0])) || (input == 'a') || (input == 'b'))
	  {
	if(d->PC->equipment[temp] != NULL)
	  {
	    if(d->PC->inventory.size() < (uint32_t) 10)
	      {
		const char* word1 =  d->PC->equipment[temp]->get_name();
		d->PC->inventory.push_back(d->PC->equipment[temp]);
		d->PC->equipment[temp] = NULL;

		const char* word2 = "You unequip the item: ";		
		char buffer[300]; 
		strncpy(buffer, word2, sizeof(buffer));
		strncat(buffer, word1, sizeof(buffer));
		
		io_queue_message(buffer);
	      }
	    else
	      {
		io_queue_message("Your Inventory is full!");
	      }
	  }
	  }
	else
	  {
	    io_queue_message("That item does not exist."); 
	  }
      }

    int tempspeed =0;
    int32_t sumspeed=0;
    int y=0;
    for(y=0;y<12;y++)
      {
	if(d->PC->equipment[y] != NULL)
	  {
	    int32_t why =  d->PC->equipment[y]->speed;
	    sumspeed  =sumspeed +why;
	  }
	
      }
    sumspeed = sumspeed + PC_SPEED;
    tempspeed = (int32_t) sumspeed;
    
 d->PC->speed = tempspeed;


    


}



void equip_item(dungeon *d)
{
  	int display =0;
	char buffer[300];

  WINDOW* subwindow = newwin(18,47,0,5);

  refresh();

  box(subwindow,0,0);
 
  mvwprintw(subwindow, 14, 1, "Enter the index of the item you want to equip");
  mvwprintw(subwindow, 1, 1, "INVENTORY--");
  uint32_t ii =0;
  uint32_t pos =2;
  int count =0;
  for(ii =0;ii< d->PC->inventory.size();ii++)
    {
    
      mvwprintw(subwindow,pos+ii,1,"(%d): ",count);
      mvwprintw(subwindow, pos+ii, 6,  d->PC->inventory[ii]->get_name() ); 
      count = count +1;
    }

    wrefresh(subwindow);
  int input = getch();
  int temp = (int)((char)input - '0');
  
    if(input == 27)
      {
	 delwin(subwindow);
      }
    else
      {
    if(d->PC->inventory.size() > 0 && (input > 0 && (uint32_t) temp < d->PC->inventory.size()  ))
      {
	if(d->PC->inventory[temp]->get_type() == object_type_t{objtype_FOOD})
	  {
	     const char* word1 =  d->PC->inventory[temp]->get_name();
	     const char* word2 = "You eat a ";
	     strncpy(buffer, word2, sizeof(buffer));
	     strncat(buffer, word1, sizeof(buffer));

	     std::string s = std::to_string(d->PC->inventory[temp]->hit);
	     s = "|| It heals " +s; 
	     char const *pchar = s.c_str();
	     strncat(buffer, pchar, sizeof(buffer));

	     io_queue_message(buffer);

	     d->PC->hp = d->PC->hp + d->PC->inventory[temp]->hit;
	     d->PC->inventory.erase(d->PC->inventory.begin() + temp );
	     refresh();
	     wrefresh(subwindow);
	     return;
	  }
	else if(d->PC->inventory[temp]->get_type() == object_type_t{objtype_CONTAINER})
	  {
	   int game =  rand() % 100 + 1;   
	   if(game >= 1 && game <= 40)
	     {
	       d->PC->gp = d->PC->gp + d->PC->inventory[temp]->value;
	       io_queue_message( "%d gold was added to your money pouch.",d->PC->inventory[temp]->value);
	     }
	   else if(game>=35 && game<= 80)
	     {
	       d->PC->hp = d->PC->hp + d->PC->inventory[temp]->hit;
	       io_queue_message("Your HP has increased by %d.",d->PC->inventory[temp]->hit);
	     }
	   else
	     {
	       d->PC->hp = floor(d->PC->hp / 2);
	       io_queue_message("Your HP has been halfed.");
	     }

	   d->PC->inventory.erase(d->PC->inventory.begin() + temp );
	   refresh();
	   wrefresh(subwindow);
	   return;
	  }
	else if(d->PC->inventory[temp]->get_type() == object_type_t{objtype_FLASK})
	  {
	    const char* word = d->PC->inventory[temp]->get_name();
	    std::string s = word;
	    if(s.compare("SpeedPotion")== 0)
	      {
		int tempspeed =0;
		int32_t sumspeed=0;
		int y=0;
		for(y=0;y<12;y++)
		  {
		    if(d->PC->equipment[y] != NULL)
		      {
			int32_t why =  d->PC->equipment[y]->speed;
			sumspeed  =sumspeed +why;
		      }
		    
		  }
		sumspeed = sumspeed + PC_SPEED;
		tempspeed = (int32_t) sumspeed;
		
		d->PC->speed = tempspeed + d->PC->inventory[temp]->speed;
		d->PC->potspeed =1;
		d->PC->spotcount = d->PC->spotcount-40;
		const char* biffer = "You drink the Speed Potion. Yuck!";
		io_queue_message(biffer);
	      }
	    else if(s.compare("DamagePotion")==0)
	      {
		d->PC->potdamage =1;
		d->PC->dpotcount = d->PC->dpotcount-40;
		const char* biffer = "You drink the Damage Potion. Yuck!";
		io_queue_message(biffer);
	      }
	      d->PC->inventory.erase(d->PC->inventory.begin() + temp );
	      refresh();
	      wrefresh(subwindow);
	      return;
	      }
	  
	  
	if(d->PC->inventory[temp]->get_type() == object_type_t{objtype_RING} || d->PC->inventory[temp]->get_type() == object_type_t{objtype_LIGHT} || d->PC->inventory[temp]->get_type() == object_type_t{objtype_BOOTS} || d->PC->inventory[temp]->get_type() == object_type_t{objtype_AMULET} || d->PC->inventory[temp]->get_type() == object_type_t{objtype_GLOVES} || d->PC->inventory[temp]->get_type() == object_type_t{objtype_HELMET} || d->PC->inventory[temp]->get_type() == object_type_t{objtype_ARMOR} || d->PC->inventory[temp]->get_type() == object_type_t{objtype_CLOAK} || d->PC->inventory[temp]->get_type() == object_type_t{objtype_WEAPON} || d->PC->inventory[temp]->get_type() == object_type_t{objtype_RANGED} || d->PC->inventory[temp]->get_type() == object_type_t{objtype_OFFHAND})
	  {
	    const char* word1 =  d->PC->inventory[temp]->get_name();
	    const char* word2 = "Item Equipped: ";
	    
	    
	    strncpy(buffer, word2, sizeof(buffer));
	    strncat(buffer, word1, sizeof(buffer));
	    display=1;
	    //io_queue_message(buffer);
	  }

	//mvwprintw(subwindow,15, 1, d->PC->inventory[temp]->get_name() );
	if(d->PC->inventory[temp]->get_type() == object_type_t{objtype_WEAPON})
	  { 
	    if(d->PC->equipment[0] == NULL)
	      {
	    d->PC->equipment[0] = d->PC->inventory[temp];
	    d->PC->inventory.erase(d->PC->inventory.begin() + temp );
	      }
	    else
	      {
		object* ttempp = d->PC->equipment[0];
		d->PC->equipment[0] = d->PC->inventory[temp];
		d->PC->inventory[temp] = ttempp;
	      }
	    //remve after testing <8>-<8>
	    //mvwprintw(subwindow,16, 1, "Weapon");
	    refresh();
	    wrefresh(subwindow);
	  }
	else if(d->PC->inventory[temp]->get_type() == object_type_t{objtype_OFFHAND})
	  { 
	    //mvwprintw(subwindow,16, 1, "OFFHAND");
	    if(d->PC->equipment[1] == NULL)
	      {
	    d->PC->equipment[1] = d->PC->inventory[temp];
	    d->PC->inventory.erase(d->PC->inventory.begin() + temp );
	      }
	    else
	      {
		object* ttempp = d->PC->equipment[1];
		d->PC->equipment[1] = d->PC->inventory[temp];
		d->PC->inventory[temp] = ttempp;
	      }
	    refresh();
	    wrefresh(subwindow);
	  }
	else if(d->PC->inventory[temp]->get_type() == object_type_t{objtype_RANGED})
	  { 
	    //mvwprintw(subwindow,16, 1, "RANGED");
	    if(d->PC->equipment[2] == NULL)
	      {
	    d->PC->equipment[2] = d->PC->inventory[temp];
	    d->PC->inventory.erase(d->PC->inventory.begin() + temp );
	      }
	    else
	      {
		object* ttempp = d->PC->equipment[2];
		d->PC->equipment[2] = d->PC->inventory[temp];
		d->PC->inventory[temp] = ttempp;
	      }
	    refresh();
	    wrefresh(subwindow);
	  }
	else if(d->PC->inventory[temp]->get_type() == object_type_t{objtype_ARMOR})
	  { 
	    //mvwprintw(subwindow,16, 1, "ARMOR");
	    if(d->PC->equipment[3] == NULL)
	      {
	    d->PC->equipment[3] = d->PC->inventory[temp];
	    d->PC->inventory.erase(d->PC->inventory.begin() + temp );
	      }
	    else
	      {
		object* ttempp = d->PC->equipment[3];
		d->PC->equipment[3] = d->PC->inventory[temp];
		d->PC->inventory[temp] = ttempp;
	      }
	    refresh();
	    wrefresh(subwindow);
	  }
	else if(d->PC->inventory[temp]->get_type() == object_type_t{objtype_HELMET})
	  { 
	    //mvwprintw(subwindow,16, 1, "HELMET");
	    if(d->PC->equipment[4] == NULL)
	      {
	    d->PC->equipment[4] = d->PC->inventory[temp];
	    d->PC->inventory.erase(d->PC->inventory.begin() + temp );
	      }
	    else
	      {
		object* ttempp = d->PC->equipment[4];
		d->PC->equipment[4] = d->PC->inventory[temp];
		d->PC->inventory[temp] = ttempp;
	      }
	    refresh();
	    wrefresh(subwindow);
	  }
	else if(d->PC->inventory[temp]->get_type() == object_type_t{objtype_CLOAK})
	  { 
	    //mvwprintw(subwindow,16, 1, "CLOAK");
	    if(d->PC->equipment[5] == NULL)
	      {
	    d->PC->equipment[5] = d->PC->inventory[temp];
	    d->PC->inventory.erase(d->PC->inventory.begin() + temp );
	      }
	    else
	      {
		object* ttempp = d->PC->equipment[5];
		d->PC->equipment[5] = d->PC->inventory[temp];
		d->PC->inventory[temp] = ttempp;
	      }
	    refresh();
	    wrefresh(subwindow);
	  }
	else if(d->PC->inventory[temp]->get_type() == object_type_t{objtype_GLOVES})
	  { 
	    // mvwprintw(subwindow,16, 1, "GLOVES");
	    if(d->PC->equipment[6] == NULL)
	      {
	    d->PC->equipment[6] = d->PC->inventory[temp];
	    d->PC->inventory.erase(d->PC->inventory.begin() + temp );
	      }
	    else
	      {
		object* ttempp = d->PC->equipment[6];
		d->PC->equipment[6] = d->PC->inventory[temp];
		d->PC->inventory[temp] = ttempp;
	      }
	    refresh();
	    wrefresh(subwindow);
	  }
	else if(d->PC->inventory[temp]->get_type() == object_type_t{objtype_BOOTS})
	  { 
	    //mvwprintw(subwindow,16, 1, "BOOTS");
	    if(d->PC->equipment[7] == NULL)
	      {
	    d->PC->equipment[7] = d->PC->inventory[temp];
	    d->PC->inventory.erase(d->PC->inventory.begin() + temp );
	      }
	    else
	      {
		object* ttempp = d->PC->equipment[7];
		d->PC->equipment[7] = d->PC->inventory[temp];
		d->PC->inventory[temp] = ttempp;
	      }
	    refresh();
	    wrefresh(subwindow);
	  }
	else if(d->PC->inventory[temp]->get_type() == object_type_t{objtype_AMULET})
	  { 
	    //mvwprintw(subwindow,16, 1, "AMULET");
	    if(d->PC->equipment[8] == NULL)
	      {
	    d->PC->equipment[8] = d->PC->inventory[temp];
	    d->PC->inventory.erase(d->PC->inventory.begin() + temp );
	      }
	    else
	      {
		object* ttempp = d->PC->equipment[8];
		d->PC->equipment[8] = d->PC->inventory[temp];
		d->PC->inventory[temp] = ttempp;
	      }
	    refresh();
	    wrefresh(subwindow);
	  }
	else if(d->PC->inventory[temp]->get_type() == object_type_t{objtype_LIGHT})
	  { 
	    //mvwprintw(subwindow,16, 1, "LIGHT");
	    if(d->PC->equipment[9] == NULL)
	      {
	    d->PC->equipment[9] = d->PC->inventory[temp];
	    d->PC->inventory.erase(d->PC->inventory.begin() + temp );
	      }
	    else
	      {
		object* ttempp = d->PC->equipment[9];
		d->PC->equipment[9] = d->PC->inventory[temp];
		d->PC->inventory[temp] = ttempp;
	      }
	    refresh();
	    wrefresh(subwindow);
	  }
	else if(d->PC->inventory[temp]->get_type() == object_type_t{objtype_RING})
	  { 
	    //mvwprintw(subwindow,16, 1, "RING");
	    if(d->PC->equipment[10] == NULL)
	      {
		d->PC->equipment[10] = d->PC->inventory[temp];
		d->PC->inventory.erase(d->PC->inventory.begin() + temp );
	      }
	    else if(d->PC->equipment[11] == NULL)
	      {
		d->PC->equipment[11] = d->PC->inventory[temp];
		d->PC->inventory.erase(d->PC->inventory.begin() + temp );
	      }
	    else
	      {
		object* ttempp = d->PC->equipment[11];
		d->PC->equipment[11] = d->PC->inventory[temp];
		d->PC->inventory[temp] = ttempp;
	      }
	    refresh();
	    wrefresh(subwindow);
	  }
       
      }
      }
    refresh();


    int tempspeed =0;
    int32_t sumspeed=0;
    int y=0;
    for(y=0;y<12;y++)
      {
	if(d->PC->equipment[y] != NULL)
	  {
	    int32_t why =  d->PC->equipment[y]->speed;
	    sumspeed  =sumspeed +why;
	  }
	
      }
    sumspeed = sumspeed + PC_SPEED;
    if(d->PC->potspeed ==1)
      {
	sumspeed=sumspeed+100;
      }
    tempspeed = (int32_t) sumspeed;

    d->PC->speed = tempspeed;


    
    //int32_t tempatk;  
  int32_t sumatk=0;
  int yy=0;
  for(yy=0;yy<12;yy++)
    {
      if(d->PC->equipment[yy] != NULL)
	    {
	      int32_t why =  d->PC->equipment[yy]->damage.roll();
	      sumatk  =sumatk +why;
	    }
      
    }
  sumatk = sumatk + d->PC->damage->roll();
  //tempatk = (int32_t) sumatk;
  
  if(display ==1)
    {
      //const char* word1 =  d->PC->inventory[temp]->get_name();
      //const char* word2 = "You equip the item: ";

      std::string s = std::to_string(sumatk);
      s = "||Stats::PC Dmg:" +s; 
      char const *pchar = s.c_str();

      std::string ss = std::to_string(sumspeed);
      ss = "::PC Spd:" +ss; 
      char const *pdchar = ss.c_str();
      //const char* word 3; 
      
      //strncpy(buffer, word2, sizeof(buffer));
      strncat(buffer, pchar, sizeof(buffer));
      strncat(buffer, pdchar, sizeof(buffer));
	    
      io_queue_message(buffer);
      io_queue_message("");
    }
    
}

