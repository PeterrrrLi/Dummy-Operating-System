#include <types.h>
#include <lib.h>
#include <synchprobs.h>
#include <synch.h>
#include <opt-A1.h>

Direction next_direction(Direction cur_direction);

Direction next_direction(Direction cur_direction) {
  if (cur_direction == east) return south;
  else if (cur_direction == south) return west;
  else if (cur_direction == west) return north;
  else return east;
}

static struct lock *intersection_lock;
static struct cv *intersection_cv;
static int num_in_intersection;
static int entry_num;
static Direction current_dir = 5;
static volatile int north_wait = 0;
static volatile int east_wait = 0;
static volatile int west_wait = 0;
static volatile int south_wait = 0;

int get_max_length(int a, int b, int c, int d);

int get_max_length(int a, int b, int c, int d) {
  if ((a >= b) && (c >= d)) {
    if (a >= c) return a;
    else return c;
  }
  else if ((a <= b) && (c >= d)) {
    if (b >= c) return b;
    else return c;
  }
  else if ((a >= b) && (c <= d)) {
    if (a >= d) return a;
    else return d;
  }
  else {
    if (b >= d) return b;
    else return d;
  }
}

Direction get_direction_with_longest_queue(void);

Direction get_direction_with_longest_queue(void) {
  int max_length = get_max_length(east_wait, south_wait, west_wait, north_wait);
  if (max_length == east_wait) return east;
  else if (max_length == south_wait) return south;
  else if (max_length == west_wait) return west;
  else return north;
}


void
intersection_sync_init(void)
{
  intersection_lock = lock_create("Lock for Intersection");
  intersection_cv = cv_create("Conditional variable for intersection");
  num_in_intersection = 0;
  entry_num = 0;
}

void
intersection_sync_cleanup(void)
{
  /* replace this default implementation with your own implementation */
  KASSERT(intersection_lock != NULL);
  KASSERT(intersection_cv != NULL);
  lock_destroy(intersection_lock);
  cv_destroy(intersection_cv);
}

void
intersection_before_entry(Direction origin, Direction destination) 
{
  (void) destination;
  lock_acquire(intersection_lock);
  while (((origin != current_dir) || (entry_num >= 5)) && (num_in_intersection != 0)) {
    if (origin == east) east_wait++;
    if (origin == north) north_wait++;
    if (origin == west) west_wait++;
    if (origin == south) south_wait++;
    cv_wait(intersection_cv, intersection_lock);
    if (origin == east) east_wait--;
    if (origin == north) north_wait--;
    if (origin == west) west_wait--;
    if (origin == south) south_wait--;
  }
  if (num_in_intersection == 0) {
    current_dir = get_direction_with_longest_queue();
  }
  KASSERT(current_dir == origin);
  entry_num++;
  num_in_intersection++;
  kprintf("In: %d to %d. Current number in intersection: %d. Current direction: %d.\n", origin, destination, num_in_intersection, current_dir);
  lock_release(intersection_lock);
}

void
intersection_after_exit(Direction origin, Direction destination) 
{
  (void) origin;
  (void) destination;
  lock_acquire(intersection_lock);
  num_in_intersection--;
  if (num_in_intersection == 0) {
    current_dir = get_direction_with_longest_queue();
    entry_num = 0;
    cv_broadcast(intersection_cv, intersection_lock); 
  }
  kprintf("Out: %d to %d. Current number in intersection: %d. Current direction: %d.\n", origin, destination, num_in_intersection, current_dir);
  lock_release(intersection_lock); 
  return;

}
