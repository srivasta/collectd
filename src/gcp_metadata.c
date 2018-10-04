/* gcp_metadata.c ---
 *
 */

/* This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation,  version 2.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; see the file COPYING.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street, Fifth
 * Floor, Boston, MA 02110-1301, USA.
 */

/* Code: */

#include <curl/curl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <yajl/yajl_tree.h>

#include "common.h"
#include "plugin.h"
#include "collectd.h"

/* Metadata can be queried at this URL from within an instance. */
#define METADATA_URL "http://metadata.google.internal/computeMetadata/v1/"
#define INSTANCE_METADATA "instance/attributes/?recursive=true"
#define INSTANCE_METADATA_URL METADATA_URL INSTANCE_METADATA

/* Maximum size of the error buffer */
#define ERRBUF_SIZE 1024

#if COLLECT_DEBUG

/**
 * @brief Safety wrappers for macros
 *  @ingroup Debugging
 *
 * Macros are dangerous if they use an  if--then--else control statement,
 * because  they may  be  used in  an   if--then--else control  statement
 * themselsves, and should be enclosed  in the following block to prevent
 * problems (like dangling else statements).
 */
#define BEGIN_BLOCK do {
#define END_BLOCK                                                              \
  }                                                                            \
  while (0)

#define ENTER_FN                                                               \
  BEGIN_BLOCK                                                                  \
  fprintf(stderr, "----------------\n");                                       \
  fprintf(stderr, "%s(%d) %s Enter\n", __FILE__, __LINE__, __func__);          \
  END_BLOCK

#define EXIT_FN                                                                \
  BEGIN_BLOCK                                                                  \
  fprintf(stderr, "%s(%d) %s Exit\n", __FILE__, __LINE__, __func__);           \
  fprintf(stderr, "----------------\n");                                       \
  END_BLOCK

#else /* COLLECT_DEBUG */

#define ENTER_FN
#define EXIT_FN

#endif /* COLLECT_DEBUG */

/** \struct A node for a linked list of key value pairs
 *
 * This data structure is a node in the linked list of key value
 * pairs, and is used for storing both the metric fields that should
 * be converted into metadata, and also the values for those fields as
 * they exist in the metadata server.
 */
struct tmp_list_s {
  char *key_p;               /*!< Pointer to the key  */
  char *value_p;             /*!< Pointer to the value  */
  struct tmp_list_s *next_p; /*!< Pointer to the next element in the list.  */
};
typedef struct tmp_list_s tmp_list_t;

/** \struct A place to push metadata from the GCP metadata server
 *
 * This data structure is used to store the data retrieved from the
 * metadata server
 */
struct metadata_s {
  char *data;  /*!< The string containing all the data  */
  size_t size; /*!< Size of the metadata retrieved  */
};
typedef struct metadata_s metadata_t;

/** \struct Linked list header
 */
struct list_container_s {
  struct tmp_list_s *next_p;
};
typedef struct list_container_s list_container_t;

/** \struct A structure for shared data passed between the callback functions
 *
 * This data structure contains the shared data used in the plugin.
 * The connection handle needs to be in a static file local variable
 * since it is needed in multiple callbacks. The mutex is to guard the
 * handle.
 */
struct gcp_metadata_handle_s {
  pthread_mutex_t lock;        /*!< Protect the handle for thread safety */
  CURL *curl_p;                /*!< Preferences for the HTTPS connection */
  struct curl_slist *headers;  /*!< A list of HTTP headers to add */
  list_container_t label_list; /* label and values to report */
  metadata_t parsed_data;
};
typedef struct gcp_metadata_handle_s gcp_metadata_handle_t;

/** \var Configuration options we care about
 *
 */
static const char *config_keys[] = {
    "ExtraMetricFields",
};
static int config_keys_num = STATIC_ARRAY_SIZE(config_keys);

/**
 *  \var The static shared data
 */
static gcp_metadata_handle_t gcp_metadata_handle = {
    PTHREAD_MUTEX_INITIALIZER, NULL, NULL, {NULL}, {NULL, 0}};

/* Forward declaration */
static void cleanup_list(list_container_t *list_head_p);
static void gcp_metadata_cleanup(void);
static tmp_list_t *create_list_node(const char *key_p, const char *value_p);
static int add_label(char *key_p, char *value_p, list_container_t *list_head_p);
static int tokenize_extra_fields(const char *value);
static size_t metadata_parse_callback(void *contents, size_t size, size_t nmemb,
                                      void *user_data_p);
static int gcp_metadata_init(void);
static int gcp_metadata_config(const char *key, const char *value);
static int gcp_metadata_submit(list_container_t *list_p);
static int gcp_metadata_read(void);
static int gcp_metadata_shutdown(void);

/**
 * \brief Cleanup the storage for meta data keys and values
 *
 * This method cleans up the storage for meta data keys and values
 *
 */
static void cleanup_list(list_container_t *list_head_p) {
  ENTER_FN;
  if (list_head_p->next_p != NULL) {
    tmp_list_t *leader_p = list_head_p->next_p;
    tmp_list_t *prev_p = leader_p;
    while (leader_p != NULL) {
      DEBUG("Cleaning up %s", "key");
      if (leader_p->key_p != NULL) {
        free(leader_p->key_p);
        leader_p->key_p = NULL;
      }
      DEBUG("Cleaning up %s", "value");
      if (leader_p->value_p != NULL) {
        free(leader_p->value_p);
        leader_p->value_p = NULL;
      }
      prev_p = leader_p;
      leader_p = leader_p->next_p;
      prev_p->next_p = NULL;
      DEBUG("Cleaning up %s", "prev");
      if (prev_p != NULL) {
        free(prev_p);
        prev_p = NULL;
      }
    }
    list_head_p->next_p = NULL;
  }
  EXIT_FN;
}

/**
 *  \brief Cleanup the shared data structure
 *
 *  This cleans up data structures and releases any resources acquired
 *  during execution. This takes no inputs, and returns nothing. This used when
 *  there are errors in initialization, and also during the final shutdown.
 */
static void gcp_metadata_cleanup(void) {
  ENTER_FN;
  if (gcp_metadata_handle.curl_p) {
    curl_easy_cleanup(gcp_metadata_handle.curl_p);
    gcp_metadata_handle.curl_p = NULL;
    if (gcp_metadata_handle.headers != NULL) {
      curl_slist_free_all(
          gcp_metadata_handle.headers); /* free the headers list */
      gcp_metadata_handle.headers = NULL;
    }
    cleanup_list(&gcp_metadata_handle.label_list);
  }
  EXIT_FN;
}

/**
 * \brief   Given a key and value, create a list node
 *
 * \details This function creates a node for the linked list that
 *           contains the metadata keys and values. It allocates
 *           memory, and copies the key and value strings. This memory
 *           has to be cleaned up after the list is done.
 *
 * \param key_p A string that contains the metadata key as a string
 * \param value_p A string that contains the metadata value as a string
 *
 * \return tmp_list_t return a node data structure, or NULL on error
 */
static tmp_list_t *create_list_node(const char *key_p, const char *value_p) {
  tmp_list_t *node_p = NULL;
  ENTER_FN;
  if (key_p == NULL) {
    EXIT_FN;
    return NULL;
  }
  node_p = (tmp_list_t *)malloc(sizeof(tmp_list_t));
  if (node_p == NULL) {
    EXIT_FN;
    return NULL; /* no memory */
  }

  node_p->next_p = NULL;
  node_p->value_p = NULL;

  node_p->key_p = malloc(strlen(key_p) + 1);
  if (node_p->key_p == NULL) {
    free(node_p);
    node_p = 0;
    EXIT_FN;
    return NULL;
  }
  strcpy(node_p->key_p, key_p);

  node_p->value_p = malloc(strlen(value_p) + 1);
  if (node_p->value_p == NULL) {
    free(node_p->key_p);
    free(node_p);
    node_p = 0;
    EXIT_FN;
    return NULL;
  }
  strcpy(node_p->value_p, value_p);
  EXIT_FN;
  return node_p;
}

/**
 * \brief This function adds a label key and value to storage
 *
 * This is separated out to allow us to change the implementation of the token
 * store.
 *
 * \param key The key to be added into the set of metric fields/
 * \param value The correspoonding value
 *
 * \return int return 0 on success, and 1 on failure (out of memory error)
 */
static int add_label(char *key_p, char *value_p,
                     list_container_t *list_head_p) {
  tmp_list_t *node_p = NULL;
  ENTER_FN;
  if (key_p == NULL) {
    EXIT_FN;
    return 1;
  }

  if (list_head_p->next_p == NULL) {
    list_head_p->next_p = node_p;
  } else {
    /*
     * Walk down the list. If we are adding just the key values we are
     * interested in, we need to walk the list to ensure the keys are unique. If
     * we are adding the value for a key we were interested in, we need to walk
     * the list to find the key.
     */
    tmp_list_t *index_p = list_head_p->next_p;
    tmp_list_t *prev_p = index_p;
    while (index_p != NULL && index_p->key_p != NULL) {
      if (strcmp(index_p->key_p, key_p) == 0) {
        if (index_p->value_p == NULL) {
          index_p->value_p = malloc(strlen(value_p) + 1);
          if (index_p->value_p == NULL) {
            EXIT_FN;
            return 1;
          }
          strcpy(index_p->value_p, value_p);
        }
        EXIT_FN;
        return 0;
      }
      prev_p = index_p;
      index_p = index_p->next_p;
    }
    node_p = create_list_node(key_p, value_p);
    if (node_p == NULL) {
      EXIT_FN;
      return 0;
    }
    prev_p->next_p = node_p;
  }
  EXIT_FN;
  return 0;
}
/**
 * \brief Get a list of extrafields from a comma separated list
 *
 * This function tokenizes a list of field names froma comma separated list in a
 * string. This is used both while reading the plugin configuration, and also
 * when we are reading the output of the metadata server. The results are
 * stashed in the global metdatata handle.
 *
 * \param value A comma searated list of tokens
 * \return int Returns 0 on success
 */
static int tokenize_extra_fields(const char *value) {
  char *saveptr;
  char *token = NULL;
  char *parse_str = NULL;
  ENTER_FN;

  char *index_str = malloc(strlen(value) + 1);
  if (index_str == NULL) {
    return 1; /* no memory */
  }
  strcpy(index_str, value);

  parse_str = index_str;
  for (;; parse_str = NULL) {
    token = strtok_r(parse_str, ",", &saveptr);
    if (token == NULL) {
      break;
    }

    if (add_label(token, NULL, &gcp_metadata_handle.label_list)) {
      fprintf(stderr, "Could not add extra field token %s", token);
      EXIT_FN;
      return 1;
    }
  }
#ifdef DEBUG_PLUGIN

  if (gcp_metadata_handle.label_list.next_p != NULL) {
    tmp_list_t *leader_p = gcp_metadata_handle.label_list.next_p;
    while (leader_p != NULL) {
      DEBUGLOG("  Token: %s", leader_p->key_p);
      leader_p = leader_p->next_p;
    }
  }

#endif // DEBUG_PLUGIN
  free(index_str);
  EXIT_FN;
  return 0;
}
/**
 * \brief    Parse JSON we retrieve using curl
 *
 * This function is passed in to libcurl, and is passed all the
 * datathat is retrieved from the metadata server. This callback
 * function gets called by libcurl as soon as there is data received
 * that needs to be saved. This callback function registered during
 * initialization of libcurl. Since the read function holds the lock when
 * libcurl is called, and thus when the callback function gets the data.
 *
 * \param contents A pointer to the retrieved data.
 * \param size The size of the data element (usually 1)
 * \param nmemb The number of data elements
 * \param user_data_p A pointer to userdata (a pointer to a struct of type
 * metadata_s)
 *
 * @return   return type
 */
static size_t metadata_parse_callback(void *contents, size_t size, size_t nmemb,
                                      void *user_data_p) {
  ENTER_FN;
  yajl_val node;
  yajl_val extra_fields;
  const char *extra_metrics_path[] = {"ExtraMetricFields", NULL};

  char errbuf[ERRBUF_SIZE];

  size_t realsize = size * nmemb;
  metadata_t *dat = (metadata_t *)user_data_p;

  /* If there was no data returned, we may have nothing. */
  if (!realsize) { /* No data returned */
    EXIT_FN;
    return 0;
  }

  dat->data = (char *)calloc(realsize + 1, 1);
  if (dat->data == NULL) {
    /* out of memory! */
    fprintf(stderr, "not enough memory (calloc returned NULL)\n");
    EXIT_FN;
    return 0;
  }
  (void)strncpy(dat->data, (const char *)contents, realsize);

  node = yajl_tree_parse((const char *)dat->data, errbuf, sizeof(errbuf));

  free(dat->data);
  dat->data = NULL;

  if (node == NULL) {
    fprintf(stderr, "parse_error: ");
    if (strlen(errbuf))
      fprintf(stderr, " %s", errbuf);
    else
      fprintf(stderr, "unknown error");
    fprintf(stderr, "\n");
    EXIT_FN;
    return 0;
  }
  /* Get data here */
  extra_fields = yajl_tree_get(node, extra_metrics_path, yajl_t_any);
  if (extra_fields != NULL) {
    if (tokenize_extra_fields(YAJL_GET_STRING(extra_fields)) > 0) {
      fprintf(stderr,
              "Could not parse the metadata field for extra metric fields");
      EXIT_FN;
      return 0;
    }
  }

  /*
   * We now have the extra fields that are configured in the metdata
   * server. Given that list, we need to extract the kay value pairs, and
   * concatenate a string
   * from the pairs
   */
  if (gcp_metadata_handle.label_list.next_p != NULL) {
    tmp_list_t *leader_p = gcp_metadata_handle.label_list.next_p;

    while (leader_p != NULL) {
      const char *metrics_path[] = {leader_p->key_p, NULL};
      yajl_val value = yajl_tree_get(node, metrics_path, yajl_t_any);
      if (value != NULL) {
        char *value_p = YAJL_GET_STRING(value);

        DEBUGLOG("   Label: %s=%s", leader_p->key_p, value_p);

        if (add_label(leader_p->key_p, value_p,
                      &gcp_metadata_handle.label_list)) {
          fprintf(stderr, "Could not add label %s", leader_p->key_p);
          EXIT_FN;
          return 1;
        }
      }
      leader_p = leader_p->next_p;
    }
  }
  yajl_tree_free(node);
  EXIT_FN;
  return realsize;
}

/**
 * \brief This This function is called once upon startup to initialize the
 *        plugin.
 *
 *  This function mostly sets up the defaults for curl. The curl handle will be
 *  reused for every read call.
 *
 *  \return int 0 on success, and a non-zero value on error (disables this
 *          plugin)
 */
static int gcp_metadata_init(void) {
  ENTER_FN;
  /* open sockets, initialize data structures, ... */
  pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);
  pthread_mutex_lock(&gcp_metadata_handle.lock);

  if (gcp_metadata_handle.curl_p == NULL) {
    gcp_metadata_handle.curl_p = curl_easy_init();
    if (gcp_metadata_handle.curl_p) {
      gcp_metadata_handle.headers =
          curl_slist_append(NULL, "Metadata-Flavor: Google");
      if (gcp_metadata_handle.headers == NULL) {
        gcp_metadata_cleanup();

        pthread_mutex_unlock(&gcp_metadata_handle.lock);
        pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);

        EXIT_FN;
        return 1;
      }

      curl_easy_setopt(gcp_metadata_handle.curl_p, CURLOPT_URL,
                       INSTANCE_METADATA_URL);
      /* tell libcurl to follow redirection */
      curl_easy_setopt(gcp_metadata_handle.curl_p, CURLOPT_FOLLOWLOCATION, 1L);
      curl_easy_setopt(gcp_metadata_handle.curl_p, CURLOPT_HTTPHEADER,
                       gcp_metadata_handle.headers);
      /* send all data to this function  */
      curl_easy_setopt(gcp_metadata_handle.curl_p, CURLOPT_WRITEFUNCTION,
                       metadata_parse_callback);

      /* we pass our parsed data struct to the callback function */
      gcp_metadata_handle.parsed_data.data = NULL;
      gcp_metadata_handle.parsed_data.size = 0;
      curl_easy_setopt(gcp_metadata_handle.curl_p, CURLOPT_WRITEDATA,
                       (void *)&gcp_metadata_handle.parsed_data);
    }
  }
  pthread_mutex_unlock(&gcp_metadata_handle.lock);
  pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);

  EXIT_FN;
  return 0;
} /* static int gcp_init (void) */

/**
 *  \brief Read the configuration information, if any
 *
 *  This function is called repeatedly with key-value pairs from the parsed
 *  configuration by collectd.
 *
 *  \param key A key (part of the key-value pair) to be processed
 *  \param value The corresponding value

 *  \return int 0 on success, greater than zero if it failed or less
 *          than zero if key has an invalid value.
 */
static int gcp_metadata_config(const char *key, const char *value) {
  ENTER_FN;

  if (value == NULL) {
    return -1;
  }
  if (key == NULL) {
    return -1;
  }
  DEBUGLOG("\tKey=%s", key);
  DEBUGLOG("\tValue=%s", value);

  if (strcmp(key, "ExtraMetricFields") != 0) {
    return -1;
  }

  pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);
  pthread_mutex_lock(&gcp_metadata_handle.lock);

  if (tokenize_extra_fields(value) > 0) {
    fprintf(stderr, "Could not parse config for extra metric fields");
  }

  pthread_mutex_unlock(&gcp_metadata_handle.lock);
  pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);

  EXIT_FN;
  return 0;
}

/**
 *  \brief Submit the data gathered to collectd
 *
 *  This is a utility function used by the read callback to populate a
 *  value_list_t and pass it to plugin_dispatch_values.
 *
 *  \param label_list_p The linked list that contains the metadata labels to
 attach to the metric
 *  \return int Return 0 on success
 */
static int gcp_metadata_submit(list_container_t *list_p) {
  value_list_t vl = VALUE_LIST_INIT;
  ENTER_FN;

  /* The read function, which is our only caller, should hold the lock */
  /* Convert the gauge_t to a value_t and add it to the value_list_t. */

  /* Lets create a dummy gauge metric to report */
  gauge_t value = 1.5L;
  vl.values = &(value_t){.gauge = value};
  vl.values_len = 1;

  /* Only set vl.time yourself if you update multiple metrics (i.e. you
   * have multiple calls to plugin_dispatch_values()) and they need to all
   * have the same timestamp. */
  /* vl.time = cdtime(); */

  strncpy(vl.plugin, "metadata", sizeof(vl.plugin));

  /* it is strongly recommended to use a type defined in the types.db file
   * instead of a custom type */
  strncpy(vl.type, "gauge", sizeof(vl.type));
  /* optionally set vl.plugin_instance and vl.type_instance to reasonable
   * values (default: "") */

  /* Create and add metadata */
  meta_data_t *md = meta_data_create();

  if (list_p->next_p != NULL) {
#ifdef BEHAVE_AS_EXEC_PLUGIN
    size_t meta_data_str_size = 0;
    char *meta_data_str_p = NULL;
#endif // BEHAVE_AS_EXEC_PLUGIN

    tmp_list_t *leader_p = list_p->next_p;
    while (leader_p != NULL) {
#ifdef BEHAVE_AS_EXEC_PLUGIN
      meta_data_str_size += 4;
#endif // BEHAVE_AS_EXEC_PLUGIN
      if (leader_p->key_p) {
#ifdef BEHAVE_AS_EXEC_PLUGIN
        meta_data_str_size += strlen(leader_p->key_p);
#endif // BEHAVE_AS_EXEC_PLUGIN
        if (leader_p->value_p) {
#ifdef BEHAVE_AS_EXEC_PLUGIN
          meta_data_str_size += strlen(leader_p->value_p);
#endif // BEHAVE_AS_EXEC_PLUGIN
          if (meta_data_add_string(md, leader_p->key_p, leader_p->value_p) <
              0) {
            fprintf(stderr, "Could not add meta data %s", leader_p->key_p);
            meta_data_destroy(md);
            EXIT_FN;
            return 1;
          }
        }
      }
      leader_p = leader_p->next_p;
    }
#ifdef BEHAVE_AS_EXEC_PLUGIN

    if (meta_data_str_size > 0) {
      meta_data_str_p = (char *)malloc(meta_data_str_size);
      if (meta_data_str_p != NULL) {
        leader_p = gcp_metadata_handle.label_list.next_p;
        size_t j = 0;

        while (leader_p != NULL) {
          size_t length = 4;
          if (leader_p->key_p) {
            length += strlen(leader_p->key_p);
            if (leader_p->value_p) {
              length += strlen(leader_p->value_p);
              snprintf(&meta_data_str_p[j], length, "s:%s=%s ", leader_p->key_p,
                       leader_p->value_p);
              meta_data_str_p[j + length - 1] = ' ';
              j += length;
            }
          }
          leader_p = leader_p->next_p;
        }
        meta_data_str_p[j - 1] = 0;
      }
    }
    time_t now = time(NULL);

    printf("PUTVAL %s/%s/%s U:%Lf\n", vl.host, vl.plugin, vl.type, 1.5L);
    printf("PUTNOTIF %s/%s/%s severity=okay time=%zu %s "
           "message=\"GCE Metadata\"\n",
           vl.host, vl.plugin, vl.type, now, meta_data_str_p);
    if (meta_data_str_p != NULL) {
      free(meta_data_str_p);
      meta_data_str_p = NULL;
    }

#endif // BEHAVE_AS_EXEC_PLUGIN
    /* We are now done with what we read */
    cleanup_list(&gcp_metadata_handle.label_list);
  }

  /* dispatch the values to collectd which passes them on to all registered
   * write functions */
  if (plugin_dispatch_values(&vl) > 0) {
    fprintf(stderr, "Could submit values and metadata");
  }
  meta_data_destroy(md);

  EXIT_FN;
  return 0;
}

/**
 *  \brief This function is called at regular intervalls to collect the data.
 *
 *  This callback function is called by collectd periodically, and should
 *  publish the metric each time it is called. Here we initiate the call to
 *  curl, to get the metadata associated with this instance. The callback
 *  defined and registered above (metadata_parse_callback), and is stashed in
 *  the global handle (gcp_metadata_handle)
 *
 *
 *  \return int Return 0 on success, and >0 on errors.
 */
static int gcp_metadata_read(void) {
  CURLcode res;

  ENTER_FN;
  pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);
  pthread_mutex_lock(&gcp_metadata_handle.lock);

  /* Perform the request, res will get the return code */
  res = curl_easy_perform(gcp_metadata_handle.curl_p);
  /* Check for errors */
  if (res != CURLE_OK) {
    fprintf(stderr, "curl_easy_perform() failed: %s\n",
            curl_easy_strerror(res));
    gcp_metadata_cleanup();

    pthread_mutex_unlock(&gcp_metadata_handle.lock);
    pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);

    EXIT_FN;
    return res;
  }

  if (gcp_metadata_submit(&gcp_metadata_handle.label_list) != 0) {
    fprintf(stderr, " Failed to submit value\n");
    pthread_mutex_unlock(&gcp_metadata_handle.lock);
    pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);

    EXIT_FN;
    return 1;
  }
  pthread_mutex_unlock(&gcp_metadata_handle.lock);
  pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);

  EXIT_FN;
  return 0;
}

/** \brief This function is called before shutting down collectd.
 *
 * This undoes everything that was done in init, and restores the
 * shared data back to the point where we found it.
 *
 * \return int 0 on success, and a non-zero value on error (disables this
 *         plugin)
 */
static int gcp_metadata_shutdown(void) {
  ENTER_FN;
  /* close sockets, free data structures, ... */
  pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);
  pthread_mutex_lock(&gcp_metadata_handle.lock);

  gcp_metadata_cleanup();

  pthread_mutex_unlock(&gcp_metadata_handle.lock);
  pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);

  EXIT_FN;
  return 0;
} /* static int my_shutdown (void) */

/*
 * This function is called after loading the plugin to register it with
 * collectd.
 */
void module_register(void) {
  /* plugin_register_data_set(&ds); */
  plugin_register_config("gcp_metadata", gcp_metadata_config, config_keys,
                         config_keys_num);
  plugin_register_read("gcp_metadata", gcp_metadata_read);
  plugin_register_init("gcp_metadata", gcp_metadata_init);
  plugin_register_shutdown("gcp_metadata", gcp_metadata_shutdown);
  return;
} /* void module_register (void) */

/* gcp_metadata.c ends here */
