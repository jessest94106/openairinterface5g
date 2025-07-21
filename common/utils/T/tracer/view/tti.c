#include "view.h"
#include "../utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <stdarg.h>
#include <string.h>
#include <math.h>

struct tti {
  view common;
  gui *g;
  widget *w;
  int automax;
  widget *w2;
  int plot;
  float refresh_rate;
  pthread_mutex_t lock;
  float data[1024*10];
  int valid[1024*10];
  float xout[1024*10];
  float yout[1024*10];
  int last_insert_point;
};

static int far_enough(int i, int last_insert, int plot_width)
{
  int p1;
  int p2;
  int hole_size_px;
  int hole_size_tti;
  hole_size_px = 10;
  hole_size_tti = 10240 * hole_size_px / plot_width;
  p1 = last_insert;
  p2 = (last_insert + hole_size_tti) % (1024*10);
  if (p1 < p2) {
    return !(i > p1 && i < p2);
  }
  return i > p2 && i <= p1;
}

static void *tti_thread(void *_this)
{
  struct tti *this = _this;
  int i;
  int length;
  int plot_width;
  int plot_height;

  while (1) {
    if (pthread_mutex_lock(&this->lock)) abort();
    xy_plot_get_dimensions(this->g, this->w, &plot_width, &plot_height);
    length = 0;
    double max = 0;
    /* TODO: optimize */
    for (i = 0; i < 1024*10; i++)
      /* do not take points too close after last insertion point */
      if (this->valid[i] &&
          far_enough(i, this->last_insert_point, plot_width)) {
        this->xout[length] = i;
        this->yout[length] = this->data[i];
        if (this->data[i] > max) max = this->data[i];
        length++;
      }
    if (this->automax) {
      char o[128];
      sprintf(o, "%d", (int)max);
      textarea_set_text(this->g, this->w2, o);
      /* for Y range we want 10, 20, 50, 100, 200, 500, etc. */
      if (max < 10) max = 10;
      double mlog = pow(10, floor(log10(max)));
      static int tolog[11] = { -1, 1, 2, 5, 5, 5, 10, 10, 10, 10, 10 };
      max = tolog[(int)ceil(max/mlog)] * mlog;
      float xmin, xmax, ymin, ymax;
      xy_plot_get_range(this->g, this->w, &xmin, &xmax, &ymin, &ymax);
      xy_plot_set_range(this->g, this->w, xmin, xmax, 0, max);
    }
    xy_plot_set_points(this->g, this->w, this->plot,
        length, this->xout, this->yout);
    if (pthread_mutex_unlock(&this->lock)) abort();
    sleepms(1000/this->refresh_rate);
  }

  return 0;
}

static void clear(view *this)
{
  /* TODO */
}

static void append(view *_this, int frame, int subframe, double value)
{
  struct tti *this = (struct tti *)_this;
  int i;
  int index = frame * 10 + subframe;

  if (pthread_mutex_lock(&this->lock)) abort();

  /* TODO: optimize */
  /* clear all between last insert point and current one
   * this may be wrong if delay between two append is
   * greater than 1024 frames (something like that)
   */
  i = (this->last_insert_point + 1) % (1024*10);
  while (i != index) {
    this->valid[i] = 0;
    i = (i + 1) % (1024*10);
  }

  this->data[index] = value;
  this->valid[index] = 1;

  this->last_insert_point = index;

  if (pthread_mutex_unlock(&this->lock)) abort();
}

view *new_view_tti(float refresh_rate, gui *g, widget *w, int color)
{
  struct tti *ret = calloc(1, sizeof(struct tti));
  if (ret == NULL) abort();

  ret->common.clear = clear;
  ret->common.append = (void (*)(view *, ...))append;

  ret->refresh_rate = refresh_rate;
  ret->g = g;
  ret->w = w;
  ret->plot = xy_plot_new_plot(g, w, color);

  ret->last_insert_point = 0;

  if (pthread_mutex_init(&ret->lock, NULL)) abort();

  new_thread(tti_thread, ret);

  return (view *)ret;
}

void view_tti_enable_automax(view *_tti, widget *w2)
{
  struct tti *tti = (struct tti *)_tti;
  tti->automax = 1;
  tti->w2 = w2;
}
