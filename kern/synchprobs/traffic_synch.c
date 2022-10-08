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
static volatile int num_in_intersection;
static volatile Direction current_dir;

void
intersection_sync_init(void)
{
  intersection_lock = lock_create("Lock for Intersection");
  intersection_cv = cv_create("Conditional variable for intersection");
  num_in_intersection = 0;
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
  while ((origin != current_dir) && (num_in_intersection != 0)) {
    cv_wait(intersection_cv, intersection_lock);
  }
  if (num_in_intersection == 0) {
    current_dir = origin;
  }
  KASSERT(current_dir == origin);
  num_in_intersection++;
  // kprintf("In: %d to %d. Current number in intersection: %d. Current direction: %d.\n", origin, destination, num_in_intersection, current_dir);
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
    current_dir = next_direction(current_dir);
    cv_broadcast(intersection_cv, intersection_lock); 
  }
  // kprintf("Out: %d to %d. Current number in intersection: %d. Current direction: %d.\n", origin, destination, num_in_intersection, current_dir);
  lock_release(intersection_lock); 
  return;
}
