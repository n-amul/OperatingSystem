#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <stdio.h>
#include "mapreduce.h"

struct map_node_t;
typedef struct map_node_t map_node_t;

typedef struct {
  map_node_t **buckets;
  unsigned nbuckets, nnodes;
} map_base_t;

typedef struct {
  unsigned bucketidx;
  map_node_t *node;
} map_iter_t;

#define map_t(T)     \
  struct {           \
    map_base_t base; \
    T *ref;          \
    T tmp;           \
  }

#define initialize_map(m) memset(m, 0, sizeof(*(m)))

#define destroy_map(m) destroy_map_(&(m)->base)

#define map_get(m, key) ((m)->ref = map_get_(&(m)->base, key))

#define map_set(m, key, value) \
  ((m)->tmp = (value), map_set_(&(m)->base, key, &(m)->tmp, sizeof((m)->tmp)))

#define delete_map_value(m, key) delete_map_value_(&(m)->base, key)

#define map_iter(m) map_iter_()

#define map_next(m, iter) map_next_(&(m)->base, iter)

void destroy_map_(map_base_t *m);
void *map_get_(map_base_t *m, const char *key);
int map_set_(map_base_t *m, const char *key, void *value, int vsize);
void delete_map_value_(map_base_t *m, const char *key);
map_iter_t map_iter_(void);
const char *map_next_(map_base_t *m, map_iter_t *iter);

typedef map_t(void *) map_void_t;
typedef map_t(char *) map_str_t;
typedef map_t(int) map_int_t;
typedef map_t(char) map_char_t;
typedef map_t(float) map_float_t;
typedef map_t(double) map_double_t;


struct map_node_t {
  unsigned hash;
  void *value;
  map_node_t *next;
};

static unsigned map_hash(const char *str) {
    unsigned hash = 2166136261u;
    while (*str) {
        hash ^= (unsigned char)*str++;
        hash *= 16777619;
    }
    return hash;
}


static map_node_t *map_newnode(const char *key, void *value, int vsize) {
  map_node_t *node;
  int ksize = strlen(key) + 1;
  int voffset = ksize + ((sizeof(void *) - ksize) % sizeof(void *));
  node = malloc(sizeof(*node) + voffset + vsize);
  if (!node) return NULL;

  memcpy(node + 1, key, ksize);
  node->hash = map_hash(key);
  node->value = ((char *)(node + 1)) + voffset;
  memcpy(node->value, value, vsize);
  return node;
}

static int map_bucketidx(map_base_t *m, unsigned hash) {
  return hash & (m->nbuckets - 1);
}

static void map_addnode(map_base_t *m, map_node_t *node) {
  int n = map_bucketidx(m, node->hash);
  node->next = m->buckets[n];
  m->buckets[n] = node;
}

static int map_resize(map_base_t *m, int nbuckets) {
  map_node_t *nodes, *node, *next;
  map_node_t **buckets;
  int i;
  nodes = NULL;
  i = m->nbuckets;
  while (i--) {
    node = (m->buckets)[i];
    while (node) {
      next = node->next;
      node->next = nodes;
      nodes = node;
      node = next;
    }
  }
  buckets = realloc(m->buckets, sizeof(*m->buckets) * nbuckets);
  if (buckets != NULL) {
    m->buckets = buckets;
    m->nbuckets = nbuckets;
  }
  if (m->buckets) {
    memset(m->buckets, 0, sizeof(*m->buckets) * m->nbuckets);
    node = nodes;
    while (node) {
      next = node->next;
      map_addnode(m, node);
      node = next;
    }
  }
  return (buckets == NULL) ? -1 : 0;
}

static map_node_t **map_getref(map_base_t *m, const char *key) {
  unsigned hash = map_hash(key);
  map_node_t **next;
  if (m->nbuckets > 0) {
    next = &m->buckets[map_bucketidx(m, hash)];
    while (*next) {
      if ((*next)->hash == hash && !strcmp((char *)(*next + 1), key)) {
        return next;
      }
      next = &(*next)->next;
    }
  }
  return NULL;
}

void destroy_map_(map_base_t *m) {
  map_node_t *next, *node;
  int i;
  i = m->nbuckets;
  while (i--) {
    node = m->buckets[i];
    while (node) {
      next = node->next;
      free(node);
      node = next;
    }
  }
  free(m->buckets);
}

void *map_get_(map_base_t *m, const char *key) {
  map_node_t **next = map_getref(m, key);
  return next ? (*next)->value : NULL;
}

int map_set_(map_base_t *m, const char *key, void *value, int vsize) {
  int n, err;
  map_node_t **next, *node;
  
  // Find the existing node
  next = map_getref(m, key);
  if (next) {
    // Deallocate old node value if sizes differ (optional: based on actual need)
    if (vsize != sizeof((*next)->value)) {
        free((*next)->value);
        (*next)->value = malloc(vsize);
    }
    // Replace the value in the existing node
    memcpy((*next)->value, value, vsize);
    return 0;
  }
  
  // Add new node
  node = map_newnode(key, value, vsize);
  if (node == NULL) goto fail;
  if (m->nnodes >= m->nbuckets) {
    n = (m->nbuckets > 0) ? (m->nbuckets << 1) : 1;
    err = map_resize(m, n);
    if (err) goto fail;
  }
  map_addnode(m, node);
  m->nnodes++;
  return 0;

fail:
  if (node) free(node);
  return -1;
}

void delete_map_value_(map_base_t *m, const char *key) {
  map_node_t *node;
  map_node_t **next = map_getref(m, key);
  if (next) {
    node = *next;
    *next = (*next)->next;
    free(node);
    m->nnodes--;
  }
}

map_iter_t map_iter_(void) {
  map_iter_t iter;
  iter.bucketidx = -1;
  iter.node = NULL;
  return iter;
}

const char *map_next_(map_base_t *m, map_iter_t *iter) {
  if (iter->node) {
    iter->node = iter->node->next;
    if (iter->node == NULL) goto nextBucket;
  } else {
  nextBucket:
    do {
      if (++iter->bucketidx >= m->nbuckets) {
        return NULL;
      }
      iter->node = m->buckets[iter->bucketidx];
    } while (iter->node == NULL);
  }
  return (char *)(iter->node + 1);
}


#include <pthread.h>
#include <semaphore.h>
#include <stdio.h>

#include "./mapreduce.h"

#define INIT_MAX_key_value_pair_PARTITION 2
typedef u_int64_t ulong;

typedef struct __key_value_pair {
  char *key;
  char *value;
} key_value_pair;

typedef struct __partition_data {
  key_value_pair **key_value_pair_list;
  ulong size_of_list;
  ulong next_to_fill;
  ulong num_keys;
  ulong *start_idxs;  
  ulong *end_idxs;
  ulong *cur_idxs;
  ulong cur_key_idx;
  map_int_t m;
  sem_t sent_flag;
  pthread_mutex_t lock;
} partition_data;

typedef struct __arg_next {
  int idx;
  pthread_mutex_t lock;  
} arg_next;

Partitioner partition_function = NULL;
Reducer reduce_function = NULL;
Mapper map_function = NULL;
int my_num_partitions = 0;
int current_reduce_partition;
int input_count = 0;
char **input_args = NULL;
arg_next my_arg_next;
partition_data **partition_data_list = NULL;
pthread_t *map_function_threads = NULL;
pthread_t *my_reducer_threads = NULL;
pthread_t *my_sort_threads = NULL;
pthread_mutex_t current_partition_lock;

ulong MR_DefaultHashPartition(char *key, int num_partitions) {
  ulong hash = 5381;
  int c;
  while ((c = *key++) != '\0') hash = hash * 33 + c;
  return hash % num_partitions;
}

ulong MR_SortedPartition(char *key, int num_partitions) {
  u_int64_t key_num = strtoul(key, NULL, 0);
  ulong assigned_partition = (key_num * num_partitions) >> 32;
  return assigned_partition;
}

void MR_Emit(char *key, char *value) {
    ulong partition_num = partition_function(key, my_num_partitions);
    partition_data *partition = partition_data_list[partition_num];

    pthread_mutex_lock(&partition->lock);

    if (partition->next_to_fill == partition->size_of_list) {
        partition->size_of_list *= 2;

        key_value_pair **new_key_value_pair_list = realloc(partition->key_value_pair_list, sizeof(key_value_pair *) * partition->size_of_list);
        if (!new_key_value_pair_list) {
            pthread_mutex_unlock(&partition->lock);
            fprintf(stderr, "Memory allocation failed in MR_Emit\n");
            exit(EXIT_FAILURE);
        }
        partition->key_value_pair_list = new_key_value_pair_list;
    }

    key_value_pair *new_pair = malloc(sizeof(key_value_pair));
    if (!new_pair) {
        pthread_mutex_unlock(&partition->lock);
        fprintf(stderr, "Memory allocation failed for key_value_pair in MR_Emit\n");
        exit(EXIT_FAILURE);
    }

    new_pair->key = strdup(key);
    new_pair->value = strdup(value);
    if (!new_pair->key || !new_pair->value) {
        // Free allocated memory if any of strdup fails
        if (new_pair->key) free(new_pair->key);
        if (new_pair->value) free(new_pair->value);
        free(new_pair);
        pthread_mutex_unlock(&partition->lock);
        fprintf(stderr, "Memory allocation failed for key/value in MR_Emit\n");
        exit(EXIT_FAILURE);
    }

    partition->key_value_pair_list[partition->next_to_fill++] = new_pair;

    pthread_mutex_unlock(&partition->lock);
}

void init_partition_data_list() {
  partition_data_list =
      (partition_data **)malloc(sizeof(partition_data *) * my_num_partitions);
  for (int i = 0; i < my_num_partitions; i++) {
    partition_data_list[i] = malloc(sizeof(partition_data));
    pthread_mutex_init(&(partition_data_list[i]->lock), NULL);
    partition_data_list[i]->size_of_list = INIT_MAX_key_value_pair_PARTITION;
    partition_data_list[i]->key_value_pair_list =
        (key_value_pair **)malloc(sizeof(key_value_pair *) * INIT_MAX_key_value_pair_PARTITION);
    memset(partition_data_list[i]->key_value_pair_list, 0,
           sizeof(key_value_pair *) * INIT_MAX_key_value_pair_PARTITION);
    partition_data_list[i]->next_to_fill = 0;
    sem_init(&partition_data_list[i]->sent_flag, 0, 0);
  }
}

void init_mapper_concurrency() {
  my_arg_next.idx = 0;
  pthread_mutex_init(&(my_arg_next.lock), NULL);
}

void init_reducer_concurrency() {
  current_reduce_partition = 0;
  pthread_mutex_init(&current_partition_lock, NULL);
}

void map_control() {
  // each mapper take one of arguments, mutual exclusion here
  int next_idx;
  while (1) {
    pthread_mutex_lock(&my_arg_next.lock);
    next_idx = my_arg_next.idx;
    my_arg_next.idx += 1;
    if (next_idx >= input_count) {
      pthread_mutex_unlock(&my_arg_next.lock);
      break;
    }
    pthread_mutex_unlock(&my_arg_next.lock);
    map_function(input_args[next_idx]);
  }
}

int key_value_pair_comparator(const void *pair1, const void *pair2) {
  key_value_pair **p1 = (key_value_pair **)pair1;
  key_value_pair **p2 = (key_value_pair **)pair2;
  return strcmp((*p1)->key, (*p2)->key);
}

void sort_partition(int partition_idx) {
    partition_data *partition = partition_data_list[partition_idx];
    
    // Sort key-value pairs
    qsort(partition->key_value_pair_list, partition->next_to_fill, sizeof(key_value_pair *),
          key_value_pair_comparator);

    ulong num_keys = 0;
    char *key_tmp = NULL;
    char *key_cmp = NULL;

    // Count unique keys
    for (size_t i = 0; i < partition->next_to_fill; i++) {
        key_cmp = partition->key_value_pair_list[i]->key;
        if (key_tmp == NULL || strcmp(key_cmp, key_tmp) != 0) {
            num_keys++;
            key_tmp = key_cmp;
        }
    }
    
    // Allocate memory for index arrays
    partition->num_keys = num_keys;
    partition->start_idxs = (ulong *)malloc(sizeof(ulong) * num_keys);
    partition->end_idxs = (ulong *)malloc(sizeof(ulong) * num_keys);
    partition->cur_idxs = (ulong *)malloc(sizeof(ulong) * num_keys);

    if (!partition->start_idxs || !partition->end_idxs || !partition->cur_idxs) {
        fprintf(stderr, "Memory allocation failed in sort_partition\n");
        exit(EXIT_FAILURE);
    }

    // Initialize the map and index arrays
    initialize_map(&partition->m);
    int cur_key_idx = -1;
    key_tmp = NULL;

    for (size_t i = 0; i < partition->next_to_fill; i++) {
        key_cmp = partition->key_value_pair_list[i]->key;

        if (key_tmp == NULL || strcmp(key_cmp, key_tmp) != 0) {
            if (cur_key_idx > -1 && cur_key_idx < num_keys) {
                partition->end_idxs[cur_key_idx] = i;
            }
            cur_key_idx++;
            if (cur_key_idx >= num_keys) {
                fprintf(stderr, "Index out-of-bounds in sort_partition\n");
                exit(EXIT_FAILURE);
            }
            partition->start_idxs[cur_key_idx] = i;
            map_set(&partition->m, key_cmp, cur_key_idx);
            partition->cur_idxs[cur_key_idx] = i;
            key_tmp = key_cmp;
        }
    }

    // Finalize the last key's end index
    if (cur_key_idx >= 0 && cur_key_idx < num_keys) {
        partition->end_idxs[cur_key_idx] = partition->next_to_fill;
    }

    partition->cur_key_idx = 0;
}


char *Get(char *key, int num_partition) {
  partition_data *partition = partition_data_list[num_partition];
  int key_idx = *map_get(&partition->m, key);
  ulong cur_key_value_pair_idx = partition->cur_idxs[key_idx];
  ulong end_idx = partition->end_idxs[key_idx];
  if (cur_key_value_pair_idx >= end_idx) {
    return NULL;
  } else {
    partition->cur_idxs[key_idx]++;
    return partition->key_value_pair_list[cur_key_value_pair_idx]->value;
  }
}

void reduce_controller() {
  int partition_to_reduce = -1;

  while (1) {
    pthread_mutex_lock(&current_partition_lock);
    partition_to_reduce = current_reduce_partition;
    if (partition_to_reduce >= my_num_partitions) {
      pthread_mutex_unlock(&current_partition_lock);
      break;
    }
    current_reduce_partition++;
    pthread_mutex_unlock(&current_partition_lock);

    partition_data *partition = partition_data_list[partition_to_reduce];
    sort_partition(partition_to_reduce);
    if (partition_to_reduce > 0) {
      sem_wait(&partition_data_list[partition_to_reduce - 1]->sent_flag);
    }

    for (size_t i = 0; i < partition->num_keys; i++) {
      ulong key_value_pair_idx = partition->start_idxs[i];
      char *key = partition->key_value_pair_list[key_value_pair_idx]->key;
      reduce_function(key, Get, partition_to_reduce);
      sem_post(&partition->sent_flag);
    }
    if (partition->num_keys == 0) {
      sem_post(&partition->sent_flag);
    }
  }
}

void destruct_partition_data_list() {
  if (!partition_data_list) return; 

  for (size_t i = 0; i < my_num_partitions; i++) {
    partition_data *partition = partition_data_list[i];
    if (!partition) continue; 
    if (partition->key_value_pair_list) {
      for (size_t j = 0; j < partition->next_to_fill; j++) {
        key_value_pair *kv_pair = partition->key_value_pair_list[j];
        if (kv_pair) {
          free(kv_pair->key);   
          free(kv_pair->value); 
          free(kv_pair);       
        }
      }
      free(partition->key_value_pair_list); 
    }

    free(partition->start_idxs); 
    free(partition->end_idxs);   
    free(partition->cur_idxs);  

    destroy_map(&partition->m);  

    pthread_mutex_destroy(&partition->lock); 
    sem_destroy(&partition->sent_flag);     
    free(partition);                         
  }

  free(partition_data_list); 
  partition_data_list = NULL; 
}


void MR_Run(int argc, char *argv[], Mapper map, int num_mappers, Reducer reduce,
            int num_reducers, Partitioner partition, int num_partitions) {
  partition_function = partition;
  my_num_partitions = num_partitions;
  map_function = map;
  reduce_function = reduce;
  input_count = argc - 1;
  input_args = argv + sizeof(char);
  init_partition_data_list();
  map_function_threads = (pthread_t *)malloc(num_mappers * sizeof(pthread_t));
  if (!map_function_threads) {
    fprintf(stderr, "Memory allocation failed for map_function_threads\n");
    exit(EXIT_FAILURE);
  }
  my_reducer_threads = (pthread_t *)malloc(num_reducers * sizeof(pthread_t));
  if (!my_reducer_threads) {
    free(map_function_threads);
    fprintf(stderr, "Memory allocation failed for my_reducer_threads\n");
    exit(EXIT_FAILURE);
  }
  init_mapper_concurrency();
  for (int i = 0; i < num_mappers; i++) {
    pthread_create(&map_function_threads[i], NULL, (void *)&map_control, NULL);
  }
  for (int i = 0; i < num_mappers; i++) {
    pthread_join(map_function_threads[i], NULL);
  }

  for (int i = 0; i < num_reducers; i++) {
    pthread_create(&my_reducer_threads[i], NULL, (void *)&reduce_controller,
                   NULL);
  }
  for (int i = 0; i < num_reducers; i++) {
    pthread_join(my_reducer_threads[i], NULL);
  }
  destruct_partition_data_list();
  free(map_function_threads);
  free(my_sort_threads);
  free(my_reducer_threads);
  return;
}