#ifndef IO_H
# define IO_H

class dungeon;

void io_init_terminal(void);
void io_reset_terminal(void);
void io_display(dungeon *d);
void io_handle_input(dungeon *d);
void io_queue_message(const char *format, ...);
void io_list_inventory(dungeon *d);
void equip_item(dungeon *d);
void io_list_equipment(dungeon *d);
void remove_item(dungeon *d);
void delete_item(dungeon *d);
void inspect_item(dungeon *d);
void drop_item(dungeon *d);
void monster_examine(dungeon *d);
void io_list_stats(dungeon *d);
void vend_items(dungeon *d);
#endif
