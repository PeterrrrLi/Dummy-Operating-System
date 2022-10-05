#include <types.h>
#include <lib.h>
#include <synchprobs.h>
#include <synch.h>
#include <opt-A1.h>

/* 
 * This simple default synchronization mechanism allows only vehicle at a time
 * into the intersection.   The intersectionSem is used as a a lock.
 * We use a semaphore rather than a lock so that this code will work even
 * before locks are implemented.
 */

/* 
 * Replace this default synchronization mechanism with your own (better) mechanism
 * needed for your solution.   Your mechanism may use any of the available synchronzation
 * primitives, e.g., semaphores, locks, condition variables.   You are also free to 
 * declare other global variables if your solution requires them.
 */

/*
 * replace this with declarations of any synchronization and other variables you need here
 */
static struct lock *intersection_lock;
static struct cv *intersection_cv;
static int num_in_intersection;
struct intersection_car {
    Direction origin;
    Direction destination;
    struct intersection_car *next;
};
static struct intersection_car *intersection_list;

/* 
 * The simulation driver will call this function once before starting
 * the simulation
 *
 * You can use it to initialize synchronization and other variables.
 * 
 */
void
intersection_sync_init(void)
{
  intersection_lock = lock_create("Lock for Intersection");
  if (intersection_lock == NULL) {
      kprintf("Error: Lock for intersection was not initialized");
      return;
  }

  intersection_cv = cv_create("Conditional variable for intersection");
  if (intersection_cv == NULL) {
      kprintf("Error: CV for intersection was not initialized");
      return;
  }
  intersection_list = NULL;
  num_in_intersection = 0;
  return;
}

/* 
 * The simulation driver will call this function once after
 * the simulation has finished
 *
 * You can use it to clean up any synchronization and other variables.
 *
 */
void
intersection_sync_cleanup(void)
{
  /* replace this default implementation with your own implementation */
  KASSERT(intersection_lock != NULL);
  KASSERT(intersection_cv != NULL);
  KASSERT(intersection_list == NULL);
  lock_destroy(intersection_lock);
  cv_destroy(intersection_cv);
}

bool if_a_car_is_making_a_right_turn(struct intersection_car *car_1);

bool if_a_car_is_making_a_right_turn(struct intersection_car *car_1) {
  if (((car_1->origin == west) && (car_1->destination == south)) ||
      ((car_1->origin == north) && (car_1->destination == west)) ||
      ((car_1->origin == east) && (car_1->destination == north)) ||
      ((car_1->origin == south) && (car_1->destination == east))) {return true;}
      else {return false;}
}

bool if_two_cars_can_be_in_intersection_together(struct intersection_car *car_1, struct intersection_car *car_2);

bool if_two_cars_can_be_in_intersection_together(struct intersection_car *car_1, struct intersection_car *car_2) {
  if (car_1->origin == car_2->origin) return true;
  else if ((car_1->origin == car_2->destination) && (car_1->destination == car_2->origin)) return true;
  else if ((car_1->destination != car_2->destination) && (if_a_car_is_making_a_right_turn(car_1) || if_a_car_is_making_a_right_turn(car_2))) return true;
  else return false;
}



/*
 * The simulation driver will call this function each time a vehicle
 * tries to enter the intersection, before it enters.
 * This function should cause the calling simulation thread 
 * to block until it is OK for the vehicle to enter the intersection.
 *
 * parameters:
 *    * origin: the Direction from which the vehicle is arriving
 *    * destination: the Direction in which the vehicle is trying to go
 *
 * return value: none
 */

void
intersection_before_entry(Direction origin, Direction destination) 
{
  // /* replace this default implementation with your own implementation */
  // (void)origin;  /* avoid compiler complaint about unused parameter */
  // (void)destination; /* avoid compiler complaint about unused parameter */
  // KASSERT(intersectionSem != NULL);
  // P(intersectionSem);
  KASSERT(intersection_lock != NULL);
  KASSERT(intersection_cv != NULL);

  struct intersection_car *new_car = kmalloc(sizeof(struct intersection_car));
  if (new_car == NULL) {
      panic("Error: New car was not initialized");
  }
  new_car->origin = origin;
  new_car->destination = destination;
  new_car->next = NULL;

  lock_acquire(intersection_lock);

  if (intersection_list == NULL) {
    intersection_list = new_car;
    KASSERT(num_in_intersection == 0);
    num_in_intersection = 1;
  } else {
    struct intersection_car *loop_ptr = intersection_list;
    while (loop_ptr != NULL) {
      if (if_two_cars_can_be_in_intersection_together(loop_ptr, new_car)) {
        loop_ptr = loop_ptr->next;
      } else {
        cv_wait(intersection_cv, intersection_lock);
        loop_ptr = intersection_list;
      }
    }
    new_car->next = intersection_list;
    intersection_list = new_car;
    num_in_intersection = num_in_intersection + 1;
  }
  lock_release(intersection_lock);
}


/*
 * The simulation driver will call this function each time a vehicle
 * leaves the intersection.
 *
 * parameters:
 *    * origin: the Direction from which the vehicle arrived
 *    * destination: the Direction in which the vehicle is going
 *
 * return value: none
 */

void
intersection_after_exit(Direction origin, Direction destination) 
{
  // /* replace this default implementation with your own implementation */
  // (void)origin;  /* avoid compiler complaint about unused parameter */
  // (void)destination; /* avoid compiler complaint about unused parameter */
  // KASSERT(intersectionSem != NULL);
  // V(intersectionSem);
  KASSERT(intersection_lock != NULL);
  KASSERT(intersection_cv != NULL);

  // acquire lock to modify intersection
  lock_acquire(intersection_lock);
  // first, find this car
  struct intersection_car *loop_ptr_1 = intersection_list;
  struct intersection_car *loop_ptr_2 = NULL;
  while (loop_ptr_1 != NULL) {
    // if this is our car
    if ((loop_ptr_1->origin == origin) && (loop_ptr_1->destination == destination)) {
      if (loop_ptr_2 != NULL) {
        loop_ptr_2->next = loop_ptr_1->next;
      } else { // if our car is the first car in our intersection car list
        intersection_list = loop_ptr_1->next;
      }
      KASSERT(loop_ptr_1 != NULL);
      kfree(loop_ptr_1); // free this car
      num_in_intersection = num_in_intersection - 1;
      cv_broadcast(intersection_cv, intersection_lock); // wake up all waiting cars
      lock_release(intersection_lock); // give up the lock for the intersection
      return;
    } else {
      loop_ptr_2 = loop_ptr_1;
      loop_ptr_1 = loop_ptr_1->next;
    }
  }
}
