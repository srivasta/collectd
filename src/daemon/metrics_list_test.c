/**
 *
 * collectd - src/daemon/metrics_list_test.c
 * Copyright (C) 2019       Google, Inc.
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
 *   Manoj Srivastava <srivasta at google.com>
 */

/* Code: */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <pthread.h>
#include "plugin.c"

#include "testing.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "config.h"
#include "globals.h"
#include "types_list.h"

#include "utils/avltree/avltree.h"
#include "utils/common/common.h" /* for STATIC_ARRAY_SIZE */

struct test_label {
  const char *key_p;
  const char *value_p;
};

/**
 * @brief      This test validates basic usage of the label store
 *
 * @details This test sets up a set of label keys and values and retrieves the
 *          value associated with a specific label.
 *
 * @return     int This returns 0 on success, and non-zero on failures
 */
DEF_TEST(list) {
  struct {
    const char *search_key_p;
    const char *result_p;
    struct test_label labels[5];
  } cases[] = {
      {.search_key_p = "key1",
       .result_p = "value1",
       .labels = {{"key1", "value1"},
                  {"Key2", "value2"},
                  {"key3", "value3"},
                  {"key4", "value4"},
                  {"key5", "value5"}}},
      {.search_key_p = "animal3",
       .result_p = "cat",
       .labels = {{"animal1", "ant"},
                  {"animal2", "bat"},
                  {"animal3", "cat"},
                  {"animal4", "dog"},
                  {"animal5", "zebra"}}},
  };
  for (size_t i = 0; i < STATIC_ARRAY_SIZE(cases); ++i) {
    char *retrieve_p = NULL;
    c_avl_tree_t *store_p =
        c_avl_create((int (*)(const void *, const void *))strcmp);
    CHECK_NOT_NULL(store_p);

    for (size_t j = 0; j < STATIC_ARRAY_SIZE(cases[i].labels); ++j) {
#pragma GCC diagnostic ignored "-Wcast-qual"
      int insert = c_avl_insert(store_p, (void *)cases[i].labels[j].key_p,
                                (void *)cases[i].labels[j].value_p);
#pragma GCC diagnostic pop
      EXPECT_EQ_INT(0, insert);
    }
#pragma GCC diagnostic ignored "-Wcast-qual"
    int retval =
        c_avl_get(store_p, (void *)cases[i].search_key_p, (void **)&retrieve_p);
#pragma GCC diagnostic pop
    EXPECT_EQ_INT(0, retval);
    EXPECT_EQ_STR(cases[i].result_p, retrieve_p);

    retrieve_p = NULL;
#pragma GCC diagnostic ignored "-Wcast-qual"
    int remove =
        c_avl_remove(store_p, (void *)cases[i].search_key_p, NULL, NULL);
#pragma GCC diagnostic pop
    EXPECT_EQ_INT(0, remove);

#pragma GCC diagnostic ignored "-Wcast-qual"
    retval =
        c_avl_get(store_p, (void *)cases[i].search_key_p, (void **)&retrieve_p);
#pragma GCC diagnostic pop
    EXPECT_EQ_INT(-1, retval);

    c_avl_destroy(store_p);
    store_p = NULL;
  }

  return 0;
}

/**
 * @brief      This test validates basic usage of the identity structure
 *
 * @details Very similar to the previous test. This test sets up a set of label
 *          keys and values in the identity struct, and retrieves the value
 *          associated with a specific label.
 *
 * @return     int This returns 0 on success, and non-zero on failures
 */
DEF_TEST(identity) {
  int retval = 0;
  struct test_label labels[][5] = {{{"key1", "value1"},
                                    {"Key2", "value2"},
                                    {"key3", "value3"},
                                    {"key4", "value4"},
                                    {"key5", "value5"}},
                                   {{"animal1", "ant"},
                                    {"animal2", "bat"},
                                    {"animal3", "cat"},
                                    {"animal4", "dog"},
                                    {"animal5", "zebra"}}};
  struct {
#pragma GCC diagnostic ignored "-Wdiscarded-qualifiers"
    const char *search_key_p;
    const char *result_p;
    int idx;
    struct identity_s id;
  } cases[] = {{.search_key_p = labels[0][0].key_p,
                .result_p = labels[0][0].value_p,
                .idx = 0,
                .id = {.name = "my-name-1", .root_p = NULL}},
               {.search_key_p = labels[1][2].key_p,
                .result_p = labels[1][2].value_p,
                .idx = 1,
                .id = {.name = "my-name-2", .root_p = NULL}}};
#pragma GCC diagnostic pop

  for (size_t i = 0; i < STATIC_ARRAY_SIZE(cases); ++i) {
    char *retrieve_p = NULL;
    cases[i].id.root_p =
        c_avl_create((int (*)(const void *, const void *))strcmp);
    CHECK_NOT_NULL(cases[i].id.root_p);
    for (size_t j = 0; j < STATIC_ARRAY_SIZE(labels[cases[i].idx]); ++j) {
#pragma GCC diagnostic ignored "-Wcast-qual"
      int insert = c_avl_insert(cases[i].id.root_p,
                                (void *)labels[cases[i].idx][j].key_p,
                                (void *)labels[cases[i].idx][j].value_p);
#pragma GCC diagnostic pop
      EXPECT_EQ_INT(0, insert);
    }
#pragma GCC diagnostic ignored "-Wcast-qual"
    retval = c_avl_get(cases[i].id.root_p, (void *)cases[i].search_key_p,
                       (void **)&retrieve_p);
#pragma GCC diagnostic pop
    EXPECT_EQ_INT(0, retval);
    EXPECT_EQ_STR(cases[i].result_p, retrieve_p);

    retrieve_p = NULL;
#pragma GCC diagnostic ignored "-Wcast-qual"
    int remove = c_avl_remove(cases[i].id.root_p, (void *)cases[i].search_key_p,
                              NULL, NULL);
#pragma GCC diagnostic pop
    EXPECT_EQ_INT(0, remove);

#pragma GCC diagnostic ignored "-Wcast-qual"
    retval = c_avl_get(cases[i].id.root_p, (void *)cases[i].search_key_p,
                       (void **)&retrieve_p);
#pragma GCC diagnostic pop
    EXPECT_EQ_INT(-1, retval);

    c_avl_destroy(cases[i].id.root_p);
    cases[i].id.root_p = NULL;
  }

  return 0;
}

/**
 * @brief      This test validates basic usage of the metric structure
 *
 * @details Very similar to the previous test. This test sets up a set of label
 *          keys and values in the metric truct, and retrieves the value
 *          associated with a specific label.
 *
 * @return     int This returns 0 on success, and non-zero on failures
 */
DEF_TEST(metrics) {
  int retval = 0;
  struct test_label labels[][5] = {{{"key1", "value1"},
                                    {"Key2", "value2"},
                                    {"key3", "value3"},
                                    {"key4", "value4"},
                                    {"key5", "value5"}},
                                   {{"animal1", "ant"},
                                    {"animal2", "bat"},
                                    {"animal3", "cat"},
                                    {"animal4", "dog"},
                                    {"animal5", "zebra"}}};
  struct {
    const char *search_key_p;
    const char *result_p;
    int idx;
    struct metric_s metric;
  } cases[] = {
      {.search_key_p = labels[0][0].key_p,
       .result_p = labels[0][0].value_p,
       .idx = 0,
       .metric = {.value = {.gauge = NAN},
                  .value_ds_type = DS_TYPE_GAUGE,
                  .type = "uptime",
                  .ds_name = "value",
                  .time = 0,
                  .interval = 0,
                  .meta = NULL,
                  .identity = NULL}},
      {.search_key_p = labels[1][2].key_p,
       .result_p = labels[1][2].value_p,
       .idx = 1,
       .metric = {.value = {.derive = 1000},
                  .value_ds_type = DS_TYPE_DERIVE,
                  .type = "cpu",
                  .ds_name = "value",
                  .time = 10,
                  .interval = 0,
                  .meta = NULL,
                  .identity = NULL}},
  };
  for (size_t i = 0; i < STATIC_ARRAY_SIZE(cases); ++i) {
    char *retrieve_p = NULL;
#pragma GCC diagnostic ignored "-Wdiscarded-qualifiers"
    identity_t identity = {.name = "TestIdentity", .root_p = NULL};
#pragma GCC diagnostic pop
    cases[i].metric.identity = clone_identity(&identity);

    CHECK_NOT_NULL(cases[i].metric.identity->root_p);
    for (size_t j = 0; j < STATIC_ARRAY_SIZE(labels[cases[i].idx]); ++j) {
#pragma GCC diagnostic ignored "-Wcast-qual"
      int insert = c_avl_insert(cases[i].metric.identity->root_p,
                                (void *)labels[cases[i].idx][j].key_p,
                                (void *)labels[cases[i].idx][j].value_p);
#pragma GCC diagnostic pop
      EXPECT_EQ_INT(0, insert);
    }
#pragma GCC diagnostic ignored "-Wcast-qual"
    retval = c_avl_get(cases[i].metric.identity->root_p,
                       (void *)cases[i].search_key_p, (void **)&retrieve_p);
#pragma GCC diagnostic pop
    EXPECT_EQ_INT(0, retval);
    EXPECT_EQ_STR(cases[i].result_p, retrieve_p);

    metric_t *cloned_metric = plugin_metric_clone(&cases[i].metric);
    CHECK_NOT_NULL(cloned_metric);
    plugin_metric_free(cloned_metric);

    retrieve_p = NULL;
#pragma GCC diagnostic ignored "-Wcast-qual"
    int remove = c_avl_remove(cases[i].metric.identity->root_p,
                              (void *)cases[i].search_key_p, NULL, NULL);
#pragma GCC diagnostic pop
    EXPECT_EQ_INT(0, remove);

#pragma GCC diagnostic ignored "-Wcast-qual"
    retval = c_avl_get(cases[i].metric.identity->root_p,
                       (void *)cases[i].search_key_p, (void **)&retrieve_p);
#pragma GCC diagnostic pop
    EXPECT_EQ_INT(-1, retval);

    c_avl_destroy(cases[i].metric.identity->root_p);
    cases[i].metric.identity->root_p = NULL;
    destroy_identity(cases[i].metric.identity);
  }
  return 0;
}

/**
 * @brief      This test validates conversion from value_list_t to metrics_t
 *
 * @details Takes value_list_t structure containing a complext metric type with
 *          more than one value and creates a metrics_list list
 *
 * @return     int This returns 0 on success, and non-zero on failures
 */
DEF_TEST(convert) {
  value_t network_metric_values[] = {
      {.derive = 120},
      {.derive = 19},
  };
  value_t load_metric_values[] = {
      {.gauge = 1},
      {.gauge = 9},
      {.gauge = 19},
  };
  const char *network_metric_subtypes[] = {
      "rx",
      "tx",
  };
  const char *network_metric_name[] = {
      "interface/if_octets/rx",
      "interface/if_octets/tx",
  };
  const char *load_metric_subtypes[] = {"shortterm", "midterm", "longterm"};
  const char *load_metric_name[] = {
      "load/load/shortterm",
      "load/load/midterm",
      "load/load/longterm",
  };
  struct {
    const char *host_expected;
    const char *plugin_expected;
    const char *type_expected;
    const char **name_expected;
    const char **subtypes_expected;
    const unsigned int subtypes_num;
    struct value_list_s metric_value;
  } cases[] = {
      {.subtypes_num = STATIC_ARRAY_SIZE(network_metric_values),
       .host_expected = "example.com",
       .plugin_expected = "interface",
       .type_expected = "if_octets",
       .name_expected = &network_metric_name[0],
       .subtypes_expected = &network_metric_subtypes[0],
       .metric_value =
           {
               .values = &network_metric_values[0],
               .values_len = STATIC_ARRAY_SIZE(network_metric_values),
               .time = TIME_T_TO_CDTIME_T_STATIC(1480063672),
               .interval = TIME_T_TO_CDTIME_T_STATIC(10),
               .host = "example.com",
               .plugin = "interface",
               .type = "if_octets",
           }},
      {.subtypes_num = STATIC_ARRAY_SIZE(load_metric_values),
       .host_expected = "example1.com",
       .plugin_expected = "load",
       .type_expected = "load",
       .name_expected = &load_metric_name[0],
       .subtypes_expected = &load_metric_subtypes[0],
       .metric_value =
           {
               .values = &load_metric_values[0],
               .values_len = STATIC_ARRAY_SIZE(load_metric_values),
               .time = TIME_T_TO_CDTIME_T_STATIC(1480063672),
               .interval = TIME_T_TO_CDTIME_T_STATIC(10),
               .host = "example1.com",
               .plugin = "load",
               .type = "load",
           }},
  };

  for (size_t i = 0; i < STATIC_ARRAY_SIZE(cases); ++i) {
    int retval = 0;
    char *host_p = NULL;
    metrics_list_t *ml = NULL;
    metrics_list_t *index_p = NULL;
    CHECK_ZERO(plugin_convert_values_to_metrics(&cases[i].metric_value, &ml));

    index_p = ml;
    EXPECT_EQ_STR(index_p->metric.type, cases[i].type_expected);

    retval = c_avl_get(index_p->metric.identity->root_p, (void *)"_host",
                       (void **)&host_p);
    EXPECT_EQ_INT(0, retval);
    EXPECT_EQ_STR(cases[i].metric_value.host, host_p);

    for (unsigned int j = 0; j < cases[i].subtypes_num; ++j) {
      EXPECT_EQ_STR(index_p->metric.ds_name, cases[i].subtypes_expected[j]);
      EXPECT_EQ_STR(index_p->metric.identity->name, cases[i].name_expected[j]);
      index_p = index_p->next_p;
    }
    destroy_metrics_list(ml);
  }
  return 0;
}

/**
 * @brief   his test validates enqueuing and dequeuing metrics from the write queue
 *
 *
 * @details Takes value_list_t structure containing a complex metric type with
 *          more than one value and adds it to the write queue, emulating a read
 *          plugin, converting it into metrics_t type objects on the fly. Then
 *          it dequeues the resulting metrics_t objects, in the manner write
 *          plugins would.
 *
 * @return     int This returns 0 on success, and non-zero on failures
 */
DEF_TEST(queue) {
  value_t network_metric_values[] = {
      {.derive = 120},
      {.derive = 19},
  };
  value_t load_metric_values[] = {
      {.gauge = 1},
      {.gauge = 9},
      {.gauge = 19},
  };

  struct {
    struct value_list_s metric_value;
  } cases[] = {
      {.metric_value =
           {
               .values = &network_metric_values[0],
               .values_len = STATIC_ARRAY_SIZE(network_metric_values),
               .time = TIME_T_TO_CDTIME_T_STATIC(1480063672),
               .interval = TIME_T_TO_CDTIME_T_STATIC(10),
               .host = "example.com",
               .plugin = "interface",
               .type = "if_octets",
           }},
      {.metric_value =
           {
               .values = &load_metric_values[0],
               .values_len = STATIC_ARRAY_SIZE(load_metric_values),
               .time = TIME_T_TO_CDTIME_T_STATIC(1480063672),
               .interval = TIME_T_TO_CDTIME_T_STATIC(10),
               .host = "example1.com",
               .plugin = "load",
               .type = "load",
           }},
  };
  plugin_init_ctx();
  for (size_t i = 0; i < STATIC_ARRAY_SIZE(cases); ++i) {
    CHECK_ZERO(plugin_dispatch_values(&cases[i].metric_value));
  }

  /* Check one metric */
  metric_t *metric_p = NULL;
  metric_p = plugin_write_dequeue();
  CHECK_NOT_NULL(metric_p);
  EXPECT_EQ_INT(TIME_T_TO_CDTIME_T_STATIC(1480063672), metric_p->time);
  EXPECT_EQ_INT(TIME_T_TO_CDTIME_T_STATIC(10), metric_p->interval);
  CHECK_NOT_NULL(metric_p->identity);
  plugin_metric_free(metric_p);

  start_write_threads(2);
  usleep(1000);
  stop_write_threads();

  return 0;
}

int main(void) {
  RUN_TEST(list);
  RUN_TEST(identity);
  RUN_TEST(metrics);
  const char *types_database = "src/types.db";
  if (access(types_database, R_OK) == -1) {
    types_database = "types.db";
    if (access(types_database, R_OK) == -1) {
      types_database = NULL;
    }
  }
  if (types_database != NULL) {
    if (read_types_list(types_database) == 0) {

#if COLLECT_DEBUG
      c_avl_iterator_t *iter_p = c_avl_get_iterator(data_sets);
      if (iter_p != NULL) {
        char *key_p = NULL;
        char *value_p = NULL;
        while ((c_avl_iterator_next(iter_p, (void **)&key_p,
                                    (void **)&value_p)) == 0) {
          ERROR("\"%s\"", key_p);
        }
      }
      c_avl_iterator_destroy(iter_p);
#endif
      RUN_TEST(convert);
      RUN_TEST(queue);
    }
  }
  END_TEST;
}

/* metric_list_test.c ends here */
