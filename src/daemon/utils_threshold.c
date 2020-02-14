/**
 * collectd - src/utils_threshold.c
 * Copyright (C) 2014       Pierre-Yves Ritschard
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Authors:
 *   Pierre-Yves Ritschard <pyr at spootnik.org>
 **/

#include "collectd.h"

#include "utils/avltree/avltree.h"
#include "utils/common/common.h"
#include "utils_threshold.h"

#include <pthread.h>

/*
 * Exported symbols
 * {{{ */
c_avl_tree_t *threshold_tree = NULL;
pthread_mutex_t threshold_lock = PTHREAD_MUTEX_INITIALIZER;
/* }}} */

/*
 * threshold_t *threshold_get
 *
 * Retrieve one specific threshold configuration. For looking up a threshold
 * matching a metric_t, see "threshold_search" below. Returns NULL if the
 * specified threshold doesn't exist.
 */
threshold_t *threshold_get(const char *hostname, const char *plugin,
                           const char *type,
                           const char *data_source) { /* {{{ */
  char name[5 * DATA_MAX_NAME_LEN];
  threshold_t *th = NULL;

  (void) snprintf(
      name, sizeof(name), "%s/%s/%s/%s", (hostname == NULL) ? "" : hostname,
      (plugin == NULL) ? "" : plugin, (type == NULL) ? "" : type,
      (data_source == NULL) ? "" : data_source);
  name[sizeof(name) - 1] = '\0';

  if (c_avl_get(threshold_tree, name, (void *)&th) == 0)
    return th;
  else
    return NULL;
} /* }}} threshold_t *threshold_get */

/*
 * threshold_t *threshold_search
 *
 * Searches for a threshold configuration using all the possible variations of
 * "Host", "Plugin", "Type", "Data Source" values. Returns NULL if no threshold
 * could be found.
 *
 * XXX: This is likely the least efficient function in collectd, and does not
 *      (yet) pay any attention to labels beyond __host__
 */
threshold_t *threshold_search(const metric_t *metric_p) { /* {{{ */
  threshold_t *th = NULL;
  if (metric_p == NULL) {
    return th;
  }
  char *host_p = NULL;
  int retval = c_avl_get(metric_p->identity->root_p, (void *)"__host__",
                         (void **)&host_p);
  if (retval != 0) {
    return th;
  }

  if ((th = threshold_get(host_p, metric_p->plugin, metric_p->type,
                          metric_p->ds->name)) != NULL)
    ;
  else if ((th = threshold_get(host_p, metric_p->plugin, metric_p->type,
                               NULL)) != NULL)
    ;
  else if ((th = threshold_get(host_p, "", metric_p->type,
                               metric_p->ds->name)) != NULL)
    ;
  else if ((th = threshold_get(host_p, "", metric_p->type, NULL)) != NULL)
    ;
  else if ((th = threshold_get("", metric_p->plugin, metric_p->type,
                               metric_p->ds->name)) != NULL)
    ;
  else if ((th = threshold_get("", metric_p->plugin, metric_p->type, NULL)) !=
           NULL)
    ;
  else if ((th = threshold_get("", "", metric_p->type, metric_p->ds->name)) !=
           NULL)
    ;
  else if ((th = threshold_get("", "", metric_p->type, NULL)) != NULL)
    ;

  sfree(host_p);
  return th;
} /* }}} threshold_t *threshold_search */

int ut_search_threshold(const metric_t *metric_p, /* {{{ */
                        threshold_t *ret_threshold) {
  threshold_t *t;

  if (metric_p == NULL)
    return EINVAL;

  /* Is this lock really necessary? */
  pthread_mutex_lock(&threshold_lock);
  t = threshold_search(metric_p);
  if (t == NULL) {
    pthread_mutex_unlock(&threshold_lock);
    return ENOENT;
  }

  memcpy(ret_threshold, t, sizeof(*ret_threshold));
  pthread_mutex_unlock(&threshold_lock);

  ret_threshold->next = NULL;

  return 0;
} /* }}} int ut_search_threshold */
