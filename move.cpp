#include "move.h"

#include <unistd.h>
#include <stdlib.h>
#include <assert.h>

#include "dungeon.h"
#include "heap.h"
#include "move.h"
#include "npc.h"
#include "pc.h"
#include "character.h"
#include "utils.h"
#include "path.h"
#include "event.h"
#include "io.h"
#include "npc.h"



void search_money(dungeon *d, pair_t p, character *def)
{
  object *o;
  std::vector<object_description> &v = d->object_descriptions;
  uint32_t i;
  for(i=0;i<v.size();i++)
    {
      if(v[i].type == object_type_t{objtype_GOLD})
	{
	  o = new object(v[i], p, d->objmap[p[dim_y]][p[dim_x]]);
	  if(def->hp > 1000 || def->damage->roll() > 30)
	    {
	      o->value = rand()%200 + 150;
	    }
	  d->objmap[p[dim_y]][p[dim_x]] = o;
	  return;
	}

    }


}
void do_combat(dungeon *d, character *atk, character *def)
{
  int can_see_atk, can_see_def;
  const char *organs[] = {
    "liver",                   /*  0 */
    "pancreas",                /*  1 */
    "heart",                   /*  2 */
    "eye",                     /*  3 */
    "arm",                     /*  4 */
    "leg",                     /*  5 */
    "intestines",              /*  6 */
    "gall bladder",            /*  7 */
    "lungs",                   /*  8 */
    "hand",                    /*  9 */
    "foot",                    /* 10 */
    "spinal cord",             /* 11 */
    "pituitary gland",         /* 12 */
    "thyroid",                 /* 13 */
    "tongue",                  /* 14 */
    "bladder",                 /* 15 */
    "diaphram",                /* 16 */
    "stomach",                 /* 17 */
    "pharynx",                 /* 18 */
    "esophagus",               /* 19 */
    "trachea",                 /* 20 */
    "urethra",                 /* 21 */
    "spleen",                  /* 22 */
    "ganglia",                 /* 23 */
    "ear",                     /* 24 */
    "subcutaneous tissue"      /* 25 */
    "cerebellum",              /* 26 */ /* Brain parts begin here */
    "hippocampus",             /* 27 */
    "frontal lobe",            /* 28 */
    "brain",                   /* 29 */
  };
  int part;

  
  if (atk != d->PC && def != d->PC) 
    { 
      int xatk = atk->position[dim_x];
      int yatk = atk->position[dim_y];
      int xdef = def->position[dim_x];
      int ydef = def->position[dim_y];
      
      d->character_map[ydef][xdef] = atk;
      d->character_map[yatk][xatk] = def;
      def->position[dim_x] = xatk;
      def->position[dim_y] = yatk;
      atk->position[dim_x] = xdef;
      atk->position[dim_y] = ydef;
      return;
    }




    if (atk != d->PC && def != d->PC) 
    { 
      return;
    }

  int32_t tempatk;
  if(atk == d->PC)
    {
      int32_t sumatk=0;
      int y=0;
      for(y=0;y<12;y++)
	{
	  if(d->PC->equipment[y] != NULL)
	    {
	        int32_t why =  d->PC->equipment[y]->damage.roll();
		sumatk  =sumatk +why;
	    }
	  
	}
      sumatk = sumatk + d->PC->damage->roll();
      tempatk = (int32_t) sumatk;
      if(d->PC->potdamage ==1)
	{
	  tempatk = tempatk+150;
	}
    }
  else
    {
      tempatk = (int32_t) atk->damage->roll();
    }


  npc *ttempp = (npc *) def;
  const char* weapon = "ShaefferGodsword";
  if((ttempp->characteristics & NPC_BOSS) && !(d->PC->equipment[0]->name.compare(weapon)==0))
    {
      tempatk =0;
    }

  std::string stringtempatk = std::to_string(tempatk);
  
  def->hp = def->hp - tempatk;

  std::string word = std::to_string(def->hp);
    if(def->hp <0)
    {
      word ="0";
    }
  std::string defword = def->name;
  defword = defword + " HP: " + word;
 
  
  std::string aword = std::to_string(atk->hp);
  std::string atkword = atk->name;

  defword = defword + "|||" + atkword + " hit " + stringtempatk + " Damage."; 

  char *cstr = new char[defword.length() + 1];
  strcpy(cstr, defword.c_str());
  io_queue_message((const char*)cstr);
  io_queue_message("");

  std::string bossword = def->name;
  //  npc *ttempp = (npc *) def;
  if(def->hp <=0 && (ttempp->characteristics & NPC_BOSS) )
      {
	d->deadboss =1;
      }


  if(def->hp <= 0)
    {
  if (def->alive) {

    
    def->alive = 0;
    search_money(d,def->position,def);
    charpair(def->position) = NULL;
    
    if (def != d->PC) {
      d->num_monsters--;
    } else {
      if ((part = rand() % (sizeof (organs) / sizeof (organs[0]))) < 26) {
        io_queue_message("As %s%s eats your %s,", is_unique(atk) ? "" : "the ",
                         atk->name, organs[rand() % (sizeof (organs) /
                                                     sizeof (organs[0]))]);
        io_queue_message("   ...you wonder if there is an afterlife.");
        /* Queue an empty message, otherwise the game will not pause for *
         * player to see above.                                          */
        io_queue_message("");
      } else {
        io_queue_message("Your last thoughts fade away as "
                         "%s%s eats your %s...",
                         is_unique(atk) ? "" : "the ",
                         atk->name, organs[part]);
        io_queue_message("");
      }
      /* Queue an empty message, otherwise the game will not pause for *
       * player to see above.                                          */
      io_queue_message("");
    }
    atk->kills[kill_direct]++;
    atk->kills[kill_avenged] += (def->kills[kill_direct] +
                                  def->kills[kill_avenged]);
  }

  if (atk == d->PC) {
    io_queue_message("You smite %s%s!", is_unique(def) ? "" : "the ", def->name);
  }

  can_see_atk = can_see(d, character_get_pos(d->PC),
                        character_get_pos(atk), 1, 0);
  can_see_def = can_see(d, character_get_pos(d->PC),
                        character_get_pos(def), 1, 0);

  if (atk != d->PC && def != d->PC) {
    if (can_see_atk && !can_see_def) {
      io_queue_message("%s%s callously murders some poor, "
                       "defenseless creature.",
                       is_unique(atk) ? "" : "The ", atk->name);
    }
    if (can_see_def && !can_see_atk) {
      io_queue_message("Something kills %s%s.",
                       is_unique(def) ? "" : "the helpless ", def->name);
    }
    if (can_see_atk && can_see_def) {
      io_queue_message("You watch in abject horror as %s%s "
                       "gruesomely murders %s%s!",
                       is_unique(atk) ? "" : "the ", atk->name,
                       is_unique(def) ? "" : "the ", def->name);
    }
  }
    }
}

void move_character(dungeon *d, character *c, pair_t next)
{
  if (charpair(next) &&
      ((next[dim_y] != c->position[dim_y]) ||
       (next[dim_x] != c->position[dim_x]))) {


    do_combat(d, c, charpair(next));
	
  } else {
    /* No character in new position. */

    d->character_map[c->position[dim_y]][c->position[dim_x]] = NULL;
    c->position[dim_y] = next[dim_y];
    c->position[dim_x] = next[dim_x];
    d->character_map[c->position[dim_y]][c->position[dim_x]] = c;
  }

  if (c == d->PC) {
    pc_reset_visibility(d->PC);
    pc_observe_terrain(d->PC, d);
  }
}

void do_moves(dungeon *d)
{
  pair_t next;
  character *c;
  event *e;

  /* Remove the PC when it is PC turn.  Replace on next call.  This allows *
   * use to completely uninit the heap when generating a new level without *
   * worrying about deleting the PC.                                       */


  if (pc_is_alive(d)) {
    /* The PC always goes first one a tie, so we don't use new_event().  *
     * We generate one manually so that we can set the PC sequence       *
     * number to zero.                                                   */
    e = (event *) malloc(sizeof (*e));
    e->type = event_character_turn;
    /* Hack: New dungeons are marked.  Unmark and ensure PC goes at d->time, *
     * otherwise, monsters get a turn before the PC.                         */
    if (d->is_new) {
      d->is_new = 0;
      e->time = d->time;
    } else {
      e->time = d->time + (1000 / d->PC->speed);
    }
    e->sequence = 0;
    e->c = d->PC;
    heap_insert(&d->events, e);
  }

  while (pc_is_alive(d) &&
         (e = (event *) heap_remove_min(&d->events)) &&
         ((e->type != event_character_turn) || (e->c != d->PC))) {
    d->time = e->time;
    if (e->type == event_character_turn) {
      c = e->c;
    }
    if (!c->alive) {
      if (d->character_map[c->position[dim_y]][c->position[dim_x]] == c) {
        d->character_map[c->position[dim_y]][c->position[dim_x]] = NULL;
      }
      if (c != d->PC) {
        event_delete(e);
      }
      continue;
    }

    npc_next_pos(d, (npc *) c, next);
    move_character(d, (npc *) c, next);

    heap_insert(&d->events, update_event(d, e, 1000 / c->speed));
  }

  io_display(d);
  if (pc_is_alive(d) && e->c == d->PC) {
    c = e->c;
    d->time = e->time;
    /* Kind of kludgey, but because the PC is never in the queue when   *
     * we are outside of this function, the PC event has to get deleted *
     * and recreated every time we leave and re-enter this function.    */
    e->c = NULL;
    event_delete(e);
    io_handle_input(d);
  }
}

void dir_nearest_wall(dungeon *d, character *c, pair_t dir)
{
  dir[dim_x] = dir[dim_y] = 0;

  if (c->position[dim_x] != 1 && c->position[dim_x] != DUNGEON_X - 2) {
    dir[dim_x] = (c->position[dim_x] > DUNGEON_X - c->position[dim_x] ? 1 : -1);
  }
  if (c->position[dim_y] != 1 && c->position[dim_y] != DUNGEON_Y - 2) {
    dir[dim_y] = (c->position[dim_y] > DUNGEON_Y - c->position[dim_y] ? 1 : -1);
  }
}

uint32_t against_wall(dungeon *d, character *c)
{
  return ((mapxy(c->position[dim_x] - 1,
                 c->position[dim_y]    ) == ter_wall_immutable) ||
          (mapxy(c->position[dim_x] + 1,
                 c->position[dim_y]    ) == ter_wall_immutable) ||
          (mapxy(c->position[dim_x]    ,
                 c->position[dim_y] - 1) == ter_wall_immutable) ||
          (mapxy(c->position[dim_x]    ,
                 c->position[dim_y] + 1) == ter_wall_immutable));
}

uint32_t in_corner(dungeon *d, character *c)
{
  uint32_t num_immutable;

  num_immutable = 0;

  num_immutable += (mapxy(c->position[dim_x] - 1,
                          c->position[dim_y]    ) == ter_wall_immutable);
  num_immutable += (mapxy(c->position[dim_x] + 1,
                          c->position[dim_y]    ) == ter_wall_immutable);
  num_immutable += (mapxy(c->position[dim_x]    ,
                          c->position[dim_y] - 1) == ter_wall_immutable);
  num_immutable += (mapxy(c->position[dim_x]    ,
                          c->position[dim_y] + 1) == ter_wall_immutable);

  return num_immutable > 1;
}

static void new_dungeon_level(dungeon *d, uint32_t dir)
{
  /* Eventually up and down will be independantly meaningful. *
   * For now, simply generate a new dungeon.                  */

  switch (dir) {
  case '<':
  case '>':
    new_dungeon(d);
    break;
  default:
    break;
  }
}


uint32_t move_pc(dungeon *d, uint32_t dir)
{
  pair_t next;
  uint32_t was_stairs = 0;
  const char *wallmsg[] = {
    "There's a wall in the way.",
    "BUMP!",
    "Ouch!",
    "You stub your toe.",
    "You can't go that way.",
    "You admire the engravings.",
    "Are you drunk?"
  };

  next[dim_y] = d->PC->position[dim_y];
  next[dim_x] = d->PC->position[dim_x];
  if( d->PC->potdamage ==1)
    {
       d->PC->dpotcount= d->PC->dpotcount+1;
    }
  if( d->PC->potspeed==1)
    {
       d->PC->spotcount = d->PC->spotcount+1;
    }


  switch (dir) {
  case 1:
  case 2:
  case 3:
    next[dim_y]++;
    break;
  case 4:
  case 5:
  case 6:
    break;
  case 7:
  case 8:
  case 9:
    next[dim_y]--;
    break;
  }
  switch (dir) {
  case 1:
  case 4:
  case 7:
    next[dim_x]--;
    break;
  case 2:
  case 5:
  case 8:
    break;
  case 3:
  case 6:
  case 9:
    next[dim_x]++;
    break;
  case '<':
    if (mappair(d->PC->position) == ter_stairs_up) {
      was_stairs = 1;
      new_dungeon_level(d, '<');
    }
    break;
  case '>':
    if (mappair(d->PC->position) == ter_stairs_down) {
      was_stairs = 1;
      new_dungeon_level(d, '>');
    }
    break;
  }

  if (was_stairs) {
    return 0;
  }

  if ((dir != '>') && (dir != '<') && (mappair(next) >= ter_floor)) {
    move_character(d, d->PC, next);
    dijkstra(d);
    dijkstra_tunnel(d);

    return 0;
  } else if (mappair(next) < ter_floor) {
    io_queue_message(wallmsg[rand() % (sizeof (wallmsg) /
                                       sizeof (wallmsg[0]))]);
    io_display(d);
  }
  if( d->PC->spotcount >=0)
    {
      d->PC->spotcount =0;
      d->PC->potspeed=0;
      
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
	      sumspeed=sumspeed+400;
	    }
	  tempspeed = (int32_t) sumspeed;
	  
	  d->PC->speed = tempspeed;
    }
  if( d->PC->dpotcount >=0)
    {
      d->PC->dpotcount =0;
      d->PC->potdamage=0;
    }
  return 1;
}
