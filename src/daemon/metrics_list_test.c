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
#include "testing.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"

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
      int insert = c_avl_insert(store_p, (void *)cases[i].labels[j].key_p,
                                (void *)cases[i].labels[j].value_p);
      EXPECT_EQ_INT(0, insert);
    }
    int retval =
        c_avl_get(store_p, (void *)cases[i].search_key_p, (void **)&retrieve_p);
    EXPECT_EQ_INT(0, retval);
    EXPECT_EQ_STR(cases[i].result_p, retrieve_p);

    retrieve_p = NULL;
    int remove =
        c_avl_remove(store_p, (void *)cases[i].search_key_p, NULL, NULL);
    EXPECT_EQ_INT(0, remove);

    retval =
        c_avl_get(store_p, (void *)cases[i].search_key_p, (void **)&retrieve_p);
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

  for (size_t i = 0; i < STATIC_ARRAY_SIZE(cases); ++i) {
    char *retrieve_p = NULL;
    cases[i].id.root_p =
        c_avl_create((int (*)(const void *, const void *))strcmp);
    CHECK_NOT_NULL(cases[i].id.root_p);
    for (size_t j = 0; j < STATIC_ARRAY_SIZE(labels[cases[i].idx]); ++j) {
      int insert = c_avl_insert(cases[i].id.root_p,
                                (void *)labels[cases[i].idx][j].key_p,
                                (void *)labels[cases[i].idx][j].value_p);
      EXPECT_EQ_INT(0, insert);
    }
    retval = c_avl_get(cases[i].id.root_p, (void *)cases[i].search_key_p,
                       (void **)&retrieve_p);
    EXPECT_EQ_INT(0, retval);
    EXPECT_EQ_STR(cases[i].result_p, retrieve_p);

    retrieve_p = NULL;
    int remove = c_avl_remove(cases[i].id.root_p, (void *)cases[i].search_key_p,
                              NULL, NULL);
    EXPECT_EQ_INT(0, remove);

    retval = c_avl_get(cases[i].id.root_p, (void *)cases[i].search_key_p,
                       (void **)&retrieve_p);
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
                  .time = 0,
                  .interval = 0,
                  .identity = NULL}},
      {.search_key_p = labels[1][2].key_p,
       .result_p = labels[1][2].value_p,
       .idx = 1,
       .metric = {.value = {.derive = 1000},
                  .value_ds_type = DS_TYPE_DERIVE,
                  .time = 10,
                  .interval = 0,
                  .identity = NULL}},
  };
  for (size_t i = 0; i < STATIC_ARRAY_SIZE(cases); ++i) {
    char *retrieve_p = NULL;
    identity_t identity = {.name = "TestIdentity", .root_p = NULL};
    cases[i].metric.identity = &identity;

    cases[i].metric.identity->root_p =
        c_avl_create((int (*)(const void *, const void *))strcmp);
    CHECK_NOT_NULL(cases[i].metric.identity->root_p);
    for (size_t j = 0; j < STATIC_ARRAY_SIZE(labels[cases[i].idx]); ++j) {
      int insert = c_avl_insert(cases[i].metric.identity->root_p,
                                (void *)labels[cases[i].idx][j].key_p,
                                (void *)labels[cases[i].idx][j].value_p);
      EXPECT_EQ_INT(0, insert);
    }
    retval = c_avl_get(cases[i].metric.identity->root_p,
                       (void *)cases[i].search_key_p, (void **)&retrieve_p);
    EXPECT_EQ_INT(0, retval);
    EXPECT_EQ_STR(cases[i].result_p, retrieve_p);

    retrieve_p = NULL;
    int remove = c_avl_remove(cases[i].metric.identity->root_p,
                              (void *)cases[i].search_key_p, NULL, NULL);
    EXPECT_EQ_INT(0, remove);

    retval = c_avl_get(cases[i].metric.identity->root_p,
                       (void *)cases[i].search_key_p, (void **)&retrieve_p);
    EXPECT_EQ_INT(-1, retval);

    c_avl_destroy(cases[i].metric.identity->root_p);
    cases[i].metric.identity->root_p = NULL;
  }
  return 0;
}

int main(void) {
  RUN_TEST(list);
  RUN_TEST(identity);
  RUN_TEST(metrics);

  END_TEST;
}

/* metric_list_test.c ends here */
