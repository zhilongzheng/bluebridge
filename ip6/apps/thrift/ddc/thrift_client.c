/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements. See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership. The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License. You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied. See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */

#include <stdio.h>
#include <glib-object.h>
#include <inttypes.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>

#include <thrift/c_glib/protocol/thrift_binary_protocol.h>
#include <thrift/c_glib/transport/thrift_buffered_udp_transport.h>
#include <thrift/c_glib/transport/thrift_udp_socket.h>
#include <thrift/c_glib/thrift.h>

#include "gen-c_glib/remote_mem_test.h"
#include "gen-c_glib/simple_arr_comp.h"
#include "lib/client_lib.h"
#include "lib/config.h"
#include "lib/utils.h"

gboolean srand_called = FALSE;

ThriftProtocol *remmem_protocol;
ThriftProtocol *arrcomp_protocol;


struct result {
  uint64_t alloc;
  uint64_t read;
  uint64_t write;
  uint64_t free;
  uint64_t rpc_start;
  uint64_t rpc_end;
};

// UTILS --> copied to server b/c it's a pain to create a shared file (build issues)
void get_args_pointer(struct in6_memaddr *ptr, struct sockaddr_in6 *targetIP) {
  // Get random memory server
  struct in6_addr *ipv6Pointer = gen_rdm_ip6_target();

  // Put it's address in targetIP (why?)
  memcpy(&(targetIP->sin6_addr), ipv6Pointer, sizeof(*ipv6Pointer));

  // Allocate memory and receive the remote memory pointer
  struct in6_memaddr temp = allocate_rmem(targetIP);

  // Copy the remote memory pointer into the give struct pointer
  memcpy(ptr, &temp, sizeof(struct in6_memaddr));
}

void marshall_shmem_ptr(GByteArray **ptr, struct in6_memaddr *addr) {
  // Blank cmd section
  //uint16_t cmd = 0u;

  // Copy subid (i.e., 103)
  *ptr = g_byte_array_append(*ptr, (const gpointer) &(addr->subid), sizeof(uint16_t));
  // Copy cmd (0)
  *ptr = g_byte_array_append(*ptr, (const gpointer) &addr->cmd, sizeof(uint16_t));
  // Copy args ()
  *ptr = g_byte_array_append(*ptr, (const gpointer) &(addr->args), sizeof(uint32_t));
  // Copy memory address (XXXX:XXXX)
  *ptr = g_byte_array_append(*ptr, (const gpointer) &(addr->paddr), sizeof(uint64_t));
}

void unmarshall_shmem_ptr(struct in6_memaddr *result_addr, GByteArray *result_ptr) {
  // Clear struct
  memset(result_addr, 0, sizeof(struct in6_memaddr));
  // Copy over received bytes
  memcpy(result_addr, result_ptr->data, sizeof(struct in6_memaddr));
}

void populate_array(uint8_t **array, int array_len, uint8_t start_num, gboolean random) {
  uint8_t num = start_num;
  if (!srand_called && random) {
    srand(time(NULL));
    srand_called = TRUE;
  }

  for (int i = 0; i < array_len; i++) {
    (*array)[i] = num;
    if (!random) {
      num++;
      num = num % UINT8_MAX; // Avoid overflow
    } else {
      num = ((uint8_t) rand()) % UINT8_MAX;
    }
  }
}

void usage(char* prog_name, char* message) {
  if (strlen(message) > 0)
    printf("ERROR: %s\n", message);
  printf("USAGE: %s -c <config> -i <num_iterations>\n", prog_name);
  printf("Where");
  printf("\t-c <config> is required and the bluebridge config (generated by mininet)\n");
  printf("\t-i <num_iterations is required and the number of iterations to run the performance test.\n");
}

// TESTS
uint64_t test_ping(RemoteMemTestIf *client, GError *error, gboolean print) {
  if(print)
    printf("Testing ping...\t\t\t");
  
  uint64_t ping_start = getns();
  gboolean success = remote_mem_test_if_ping (client, &error);
  uint64_t ping_time = getns() - ping_start;

  if (print){
    if (success) {
      printf("SUCCESS\n");
    } else {
      printf("FAILED\n");
      printf("\tERROR: %s\n", error->message);
      g_clear_error (&error);
    }
  }
  return ping_time;
}

uint64_t test_server_alloc(RemoteMemTestIf *client, GByteArray **res, CallException *exception, GError *error, gboolean print) {
  if (print)
    printf("Testing allocate_mem...\t\t");
  
  uint64_t alloc_start = getns();
  gboolean success = remote_mem_test_if_allocate_mem(client, res, BLOCK_SIZE, &exception, &error);
  uint64_t alloc_time = getns() - alloc_start;

  if (print) {
    if (success) {
      printf("SUCCESS\n");
    } else {
      // TODO add exception case
      printf("FAILED\n");
      printf("\tERROR: %s\n", error->message);
      g_clear_error (&error);
    }
  }

  return alloc_time;
}

uint64_t test_server_write(RemoteMemTestIf *client, GByteArray *res, CallException *exception, GError *error, gboolean print) {
  if (print)
    printf("Testing write_mem...\t\t");
  // Clear payload
  char *payload = malloc(BLOCK_SIZE);
  snprintf(payload, 50, "HELLO WORLD! How are you?");

  uint64_t write_start = getns();
  gboolean success = remote_mem_test_if_write_mem(client, res, payload, &exception, &error);
  uint64_t write_time = getns() - write_start;

  if (print) {
    if (success) {
      printf ("SUCCESS\n");
    } else {
      // TODO: add exception case
      printf("FAILED\n");
      printf("\tERROR: %s\n", error->message);
      g_clear_error (&error);
    }
  }

  free(payload);
  return write_time;
}

uint64_t test_server_read(RemoteMemTestIf *client, GByteArray *res, CallException *exception, GError *error, gboolean print) {
  if (print)
    printf("Testing read_mem...\t\t");
  
  uint64_t read_start = getns();
  gboolean success = remote_mem_test_if_read_mem(client, res, &exception, &error);
  uint64_t read_time = getns() - read_start;

  if (print) {
    if (success) {
      printf ("SUCCESS\n");
    } else {
      printf("FAILED\n");
      printf("\tERROR: %s\n", error->message);
      g_clear_error (&error);
    }
  }

  return read_time;
}

uint64_t test_server_free(RemoteMemTestIf *client, GByteArray *res, CallException *exception, GError *error, gboolean print) {
  if (print)
    printf("Testing free_mem...\t\t");
  
  uint64_t free_start = getns();
  gboolean success = remote_mem_test_if_free_mem(client, res, &exception, &error);
  uint64_t free_time = getns() - free_start;

  if (print) {
    if (success) {
      printf ("SUCCESS\n");
    } else {
      printf("FAILED\n");
      printf("\tERROR: %s\n", error->message);
      g_clear_error (&error);
    }
  }

  return free_time;
}

void test_server_functionality(RemoteMemTestIf *client) {
  GError *error = NULL;
  CallException *exception = NULL;
  GByteArray* res = NULL;

  test_ping(client, error, TRUE);

  test_server_alloc(client, &res, exception, error, TRUE);

  test_server_write(client, res, exception, error, TRUE);

  test_server_read(client, res, exception, error, TRUE);

  test_server_free(client, res, exception, error, TRUE);
}

struct result test_increment_array(SimpleArrCompIf *client, int size, struct sockaddr_in6 *targetIP, gboolean print) {
  GError *error = NULL;                       // Error (in transport, socket, etc.)
  CallException *exception = NULL;            // Exception (thrown by server)
  struct in6_memaddr args_addr;               // Shared memory address of the argument pointer
  GByteArray* args_ptr = g_byte_array_new();  // Argument pointer
  GByteArray* result_ptr = NULL;              // Result pointer
  uint8_t *arr;                               // Array to be sent (must be uint8_t to match char size)
  int arr_len = size;                           // Size of array to be sent
  uint8_t incr_val = 1;                       // Value to increment each value in the array by
  struct result res;
  uint64_t start;
  if (print)
    printf("Testing increment_array...\t");

  // Get a shared memory pointer for argument array
  // TODO: add individual timers here to get breakdown of costs. 
  start = getns();
  get_args_pointer(&args_addr, targetIP);
  res.alloc = getns() - start;

  // Create argument array
  arr = malloc(arr_len*sizeof(uint8_t));
  populate_array(&arr, arr_len, 0, FALSE);

  char temp[BLOCK_SIZE];

  memcpy(temp, arr, arr_len);

  start = getns();
  // Write array to shared memory
  write_rmem(targetIP, (char*) temp, &args_addr);
  res.write = getns() - start;

  // Marshall shared pointer address
  marshall_shmem_ptr(&args_ptr, &args_addr);

  res.rpc_start = getns();
  // CALL RPC
  simple_arr_comp_if_increment_array(client, &result_ptr, args_ptr, incr_val, arr_len, &exception, &error);
  res.rpc_end = getns();

  if (print) {
    if (error) {
      printf ("ERROR: %s\n", error->message);
      g_clear_error (&error);
    } else if (exception) {
      gint32 err_code;
      gchar *message;

      g_object_get(exception, "message", &message,
                              "err_code", &err_code,
                              NULL);

      // TODO: make a print macro that changes the message based on the error_code
      printf("EXCEPTION %d: %s\n", err_code, message);
    }
  }

  // Unmarshall shared pointer address
  struct in6_memaddr result_addr;
  unmarshall_shmem_ptr(&result_addr, result_ptr);

  // Create result array to read into
  char* result_arr = malloc(arr_len);

  start = getns();
  // Read in result array
  get_rmem(result_arr, arr_len, targetIP, &result_addr);
  res.read = getns() - start;

  if (print) {
    // Make sure the server returned the correct result
    for (int i = 0; i < arr_len; i++) {
      if ((uint8_t) result_arr[i] != arr[i] + incr_val) {
        printf("FAILED: result (%d) does not match expected result (%d) at index (%d)\n", (uint8_t) result_arr[i], arr[i] + incr_val, i);
      }
    }
  }

  // Free malloc'd and GByteArray memory
  free(result_arr);
  free(arr);

  start = getns();
  free_rmem(targetIP, &args_addr);
  free_rmem(targetIP, &result_addr);
  res.free = getns() - start;
  // g_byte_array_free(args_ptr, TRUE);  // We allocated this, so we free it
  // g_byte_array_unref(result_ptr);     // We only received this, so we dereference it

  if (print)
    printf("SUCCESS\n");

  return res;
}

struct result test_add_arrays(SimpleArrCompIf *client, int size, struct sockaddr_in6 *targetIP, gboolean print) {
  GError *error = NULL;                       // Error (in transport, socket, etc.)
  CallException *exception = NULL;            // Exception (thrown by server)
  struct in6_memaddr arg1_addr;               // Shared memory address of the argument pointer
  struct in6_memaddr arg2_addr;               // Shared memory address of the argument pointer
  GByteArray* arg1_ptr = g_byte_array_new();  // Argument pointer
  GByteArray* arg2_ptr = g_byte_array_new();  // Argument pointer
  GByteArray* result_ptr = NULL;              // Result pointer
  struct in6_memaddr result_addr;             // Shared memory address of result
  uint8_t *arr1;                              // Array 1 to be added
  uint8_t *arr2;                              // Array 2 to be added
  int arrays_len = size;                        // Size of array to be sent
  struct result res;
  uint64_t start;
  res.free = 0;

  if (print)
    printf("Testing add_arrays...\t\t");

  // Get pointers for arrays
  start = getns();
  get_args_pointer(&arg1_addr, targetIP);
  get_args_pointer(&arg2_addr, targetIP);
  res.alloc = getns() - start;

  // Populate arrays
  arr1 = malloc(arrays_len*sizeof(uint8_t));
  arr2 = malloc(arrays_len*sizeof(uint8_t));
  populate_array(&arr1, arrays_len, 3, TRUE);
  populate_array(&arr2, arrays_len, 5, TRUE);

  char temp1[BLOCK_SIZE];
  char temp2[BLOCK_SIZE];

  memcpy(temp1, arr1, arrays_len);
  memcpy(temp2, arr2, arrays_len);

  // Write arrays to shared memory
  start = getns();
  write_rmem(targetIP, (char*) temp1, &arg1_addr);
  write_rmem(targetIP, (char*) temp2, &arg2_addr);
  res.write = getns() - start;

  // Marshall shared pointer addresses
  marshall_shmem_ptr(&arg1_ptr, &arg1_addr);
  marshall_shmem_ptr(&arg2_ptr, &arg2_addr);

  // CALL RPC
  res.rpc_start = getns();
  simple_arr_comp_if_add_arrays(client, &result_ptr, arg1_ptr, arg2_ptr, arrays_len, &exception, &error);
  res.rpc_end = getns();

  if (error) {
    printf ("ERROR: %s\n", error->message);
    g_clear_error (&error);
  } else if (exception) {
    gint32 err_code;
    gchar *message;

    g_object_get(exception, "message", &message,
                            "err_code", &err_code,
                            NULL);

    // TODO: make a print macro that changes the message based on the error_code
    printf("EXCEPTION %d: %s\n", err_code, message);
  } else {
    // Unmarshall shared pointer address
    unmarshall_shmem_ptr(&result_addr, result_ptr);

    // Create result array to read into
    char* result_arr = malloc(arrays_len);

    // Read in result array
    start = getns();
    get_rmem(result_arr, arrays_len, targetIP, &result_addr);
    res.read = getns() - start;

    if (print) {
      // Make sure the server returned the correct result
      uint8_t expected = 0;
      for (int i = 0; i < arrays_len; i++) {
        expected = arr1[i] + arr2[i];
        if ((uint8_t) result_arr[i] != (uint8_t) (arr1[i] + arr2[i])) {
          printf("FAILED: result (%d) does not match expected result (%d) at index (%d)\n", (uint8_t) result_arr[i], expected, i);
          goto cleanupres;
        }
      }

      printf("SUCCESS\n");
    }
cleanupres:
    free(result_arr);
    start = getns();
    free_rmem(targetIP, &result_addr);
    res.free += getns() - start;
  }

  // Free malloc'd and GByteArray memory
  free(arr1);
  free(arr2);
  start = getns();
  free_rmem(targetIP, &arg1_addr);    // Free the shared memory
  free_rmem(targetIP, &arg2_addr);    // Free the shared memory
  res.free += getns() - start;
  // g_byte_array_free(arg1_ptr, TRUE);  // We allocated this, so we free it
  // g_byte_array_free(arg2_ptr, TRUE);  // We allocated this, so we free it
  // g_byte_array_unref(result_ptr);     // We only received this, so we dereference it

  return res;
}

void mat_multiply(SimpleArrCompIf *client, struct sockaddr_in6 *targetIP) {
  // GError *error = NULL;                       // Error (in transport, socket, etc.)
  // CallException *exception = NULL;            // Exception (thrown by server)
  // struct in6_memaddr args_addr;               // Shared memory address of the argument pointer
  // GByteArray* args_ptr = g_byte_array_new();  // Argument pointer
  // GByteArray* result_ptr = NULL;              // Result pointer

  THRIFT_UNUSED_VAR(client);
  THRIFT_UNUSED_VAR(targetIP);
}

void word_count(SimpleArrCompIf *client, struct sockaddr_in6 *targetIP) {
  // GError *error = NULL;                       // Error (in transport, socket, etc.)
  // CallException *exception = NULL;            // Exception (thrown by server)
  // struct in6_memaddr args_addr;               // Shared memory address of the argument pointer
  // GByteArray* args_ptr = g_byte_array_new();  // Argument pointer
  // GByteArray* result_ptr = NULL;              // Result pointer
  THRIFT_UNUSED_VAR(client);
  THRIFT_UNUSED_VAR(targetIP);
}

void sort_array(SimpleArrCompIf *client, struct sockaddr_in6 *targetIP) {
  // GError *error = NULL;                       // Error (in transport, socket, etc.)
  // CallException *exception = NULL;            // Exception (thrown by server)
  // struct in6_memaddr args_addr;               // Shared memory address of the argument pointer
  // GByteArray* args_ptr = g_byte_array_new();  // Argument pointer
  // GByteArray* result_ptr = NULL;              // Result pointer
  THRIFT_UNUSED_VAR(client);
  THRIFT_UNUSED_VAR(targetIP);
}

uint64_t no_op_rpc(SimpleArrCompIf *client, int size, struct sockaddr_in6 *targetIP) {
  GError *error = NULL;                       // Error (in transport, socket, etc.)
  struct in6_memaddr args_addr;               // Shared memory address of the argument pointer
  GByteArray* args_ptr = g_byte_array_new();  // Argument pointer
  GByteArray* result_ptr = NULL;              // Result pointer
  uint8_t *arr;                               // Array to be sent (must be uint8_t to match char size)
  int arr_len = size;                           // Size of array to be sent
  uint64_t start, rpc_time;


  // Get a shared memory pointer for argument array
  get_args_pointer(&args_addr, targetIP);

  // Create argument array
  arr = malloc(arr_len*sizeof(uint8_t));
  populate_array(&arr, arr_len, 0, FALSE);

  char temp[BLOCK_SIZE];
  memcpy(temp, arr, arr_len);

  // Write array to shared memory
  write_rmem(targetIP, (char*) temp, &args_addr);

  // Marshall shared pointer address
  marshall_shmem_ptr(&args_ptr, &args_addr);

  start = getns();
  // CALL RPC
  simple_arr_comp_if_no_op(client, &result_ptr, args_ptr, arr_len, &error);
  rpc_time = getns() - start;

  // Unmarshall shared pointer address
  struct in6_memaddr result_addr;
  unmarshall_shmem_ptr(&result_addr, result_ptr);

  // Create result array to read into
  char* result_arr = malloc(arr_len);

  // Read in result array
  get_rmem(result_arr, arr_len, targetIP, &result_addr);

  // Free malloc'd and GByteArray memory
  free(result_arr);
  free(arr);
  free_rmem(targetIP, &args_addr);
  free_rmem(targetIP, &result_addr);
  // g_byte_array_free(args_ptr, TRUE);  // We allocated this, so we free it
  // g_byte_array_unref(result_ptr);     // We only received this, so we dereference it

  return rpc_time;
}

void test_shared_pointer_rpc(SimpleArrCompIf *client, struct sockaddr_in6 *targetIP) {
  test_increment_array(client, BLOCK_SIZE, targetIP, TRUE);
  test_add_arrays(client, BLOCK_SIZE, targetIP, TRUE);
  // mat_multiply(client, targetIP);
  // word_count(client, targetIP);
  // sort_array(client, targetIP);
}

void microbenchmark_perf(RemoteMemTestIf *client, int iterations) {
  GError *error = NULL;
  CallException *exception = NULL;

  // Call perf test for ping
  uint64_t *ping_times = malloc(iterations*sizeof(uint64_t));
  uint64_t ping_total = 0;
  uint64_t *alloc_times = malloc(iterations*sizeof(uint64_t));
  uint64_t alloc_total = 0;
  uint64_t *write_times = malloc(iterations*sizeof(uint64_t));
  uint64_t write_total = 0;
  uint64_t *read_times = malloc(iterations*sizeof(uint64_t));
  uint64_t read_total = 0;
  uint64_t *free_times = malloc(iterations*sizeof(uint64_t));
  uint64_t free_total = 0;

  for (int i = 0; i < iterations; i++) {
    GByteArray* res = NULL;

    ping_times[i] = test_ping(client, error, FALSE);
    ping_total += ping_times[i];

    alloc_times[i] = test_server_alloc(client, &res, exception, error, FALSE);
    alloc_total += alloc_times[i];

    write_times[i] = test_server_write(client, res, exception, error, FALSE);
    write_total += write_times[i];

    read_times[i] = test_server_read(client, res, exception, error, FALSE);
    read_total += read_times[i];

    free_times[i] = test_server_free(client, res, exception, error, FALSE);
    free_total += free_times[i];
  }

  printf("Average %s latency: "KRED"%lu us\n"RESET, "ping", ping_total / (iterations*1000));
  printf("Average %s latency: "KRED"%lu us\n"RESET, "alloc", alloc_total / (iterations*1000));
  printf("Average %s latency: "KRED"%lu us\n"RESET, "write", write_total / (iterations*1000));
  printf("Average %s latency: "KRED"%lu us\n"RESET, "read", read_total / (iterations*1000));
  printf("Average %s latency: "KRED"%lu us\n"RESET, "free", free_total / (iterations*1000));

  free(ping_times);
  free(alloc_times);
  free(write_times);
  free(read_times);
  free(free_times);
}

void no_op_perf(SimpleArrCompIf *client, struct sockaddr_in6 *targetIP,
                int iterations, int max_size, int incr) {
  // uint64_t *no_op_rpc_times = malloc(iterations*sizeof(uint64_t));
  uint64_t no_op_rpc_total;
  int s = 4095;

  // for (int s = 10; s < max_size; s+= incr) {
    no_op_rpc_total = 0;
    for (int i = 0; i < iterations; i++) {
      no_op_rpc_total += no_op_rpc(client, s, targetIP);
       // no_op_rpc_times[i];
    }
    printf("Average %s latency (%d): "KRED"%lu us\n"RESET, "no_op_rpcs", s, no_op_rpc_total / (iterations*1000));
  // }


  // free(no_op_rpc_times);
}

void create_dir(const char *name) {
    struct stat st = {0};
    if (stat(name, &st) == -1) {
        int MAX_FNAME = 256;
        char fname[MAX_FNAME];
        snprintf(fname, MAX_FNAME, "mkdir -p %s -m 777", name);
        printf("Creating dir %s...\n", name);
        system(fname);
    }
}

FILE* generate_file_handle(char* method_name, char* operation, int size) {
  int len = strlen(method_name) + strlen(operation) + strlen("./prelim_results/bifrost___.txt") + 10;
  // printf("Length: %d, method_name: %d, operation: %d, everything else: %d\n", len, strlen(method_name), strlen(operation), strlen("./prelim_results/bifrost___.txt") + 4);
  int wrote = 0;
  char temp[len + 1];
  create_dir("./prelim_results/");
  if (size > -1) {
    wrote = snprintf(temp, len, "./prelim_results/bifrost_%s_%s_%d.txt", method_name, operation, size);
  } else {
    wrote = snprintf(temp, len, "./prelim_results/bifrost_%s_%s.txt", method_name, operation);
  }

  // printf("wrote: %d\n", wrote);

  return fopen(temp, "w");
}

void increment_array_perf(SimpleArrCompIf *client, 
                          struct sockaddr_in6 *targetIP, int iterations, 
                          int max_size, int incr, char* method_name) {

  uint64_t alloc_times = 0, read_times = 0, write_times = 0, free_times = 0;
  FILE* alloc_file = generate_file_handle(method_name, "alloc", -1);
  FILE* read_file = generate_file_handle(method_name, "read", -1);
  FILE* write_file = generate_file_handle(method_name, "write", -1);
  FILE* free_file = generate_file_handle(method_name, "free", -1);
  fprintf(alloc_file, "size,avg latency\n");
  fprintf(read_file, "size,avg latency\n");
  fprintf(write_file, "size,avg latency\n");
  fprintf(free_file, "size,avg latency\n");

  for (int s = 0; s < max_size; s+= incr) {
    FILE* rpc_start_file = generate_file_handle(method_name, "rpc_start", s);
    FILE* rpc_end_file = generate_file_handle(method_name, "rpc_end", s);
    FILE* send_file = generate_file_handle(method_name, "c1_send", s);
    FILE* recv_file = generate_file_handle(method_name, "c1_recv", s);
    alloc_times = 0;
    read_times = 0;
    write_times = 0;
    free_times = 0;
    for (int i = 0; i < iterations; i++) {
      struct result res = test_increment_array(client, s, targetIP, FALSE);
      alloc_times += res.alloc;
      read_times += res.read;
      write_times += res.write;
      free_times += res.free;
      fprintf(rpc_start_file, "%lu\n", res.rpc_start);
      fprintf(rpc_end_file, "%lu\n", res.rpc_end);
    }
    fprintf(alloc_file, "%d,%lu\n", s, alloc_times / (iterations*1000) );
    fprintf(read_file, "%d,%lu\n", s, read_times / (iterations*1000) );
    fprintf(write_file, "%d,%lu\n", s, write_times / (iterations*1000) );
    fprintf(free_file, "%d,%lu\n", s, free_times / (iterations*1000) );
    thrift_protocol_flush_timestamps(arrcomp_protocol, send_file, THRIFT_PERF_SEND, TRUE);
    thrift_protocol_flush_timestamps(arrcomp_protocol, recv_file, THRIFT_PERF_RECV, TRUE);
    fclose(send_file);
    fclose(recv_file);
    fclose(rpc_start_file);
    fclose(rpc_end_file);
  }
  fclose(alloc_file);
  fclose(read_file);
  fclose(write_file);
  fclose(free_file);
}

void add_arrays_perf(SimpleArrCompIf *client, 
                     struct sockaddr_in6 *targetIP, int iterations, 
                     int max_size, int incr, char* method_name) {
  uint64_t alloc_times = 0, read_times = 0, write_times = 0, free_times = 0;
  FILE* alloc_file = generate_file_handle(method_name, "alloc", -1);
  FILE* read_file = generate_file_handle(method_name, "read", -1);
  FILE* write_file = generate_file_handle(method_name, "write", -1);
  FILE* free_file = generate_file_handle(method_name, "free", -1);

  fprintf(alloc_file, "size,avg latency\n");
  fprintf(read_file, "size,avg latency\n");
  fprintf(write_file, "size,avg latency\n");
  fprintf(free_file, "size,avg latency\n");

  for (int s = 0; s < max_size; s+= incr) {
    FILE* rpc_start_file = generate_file_handle(method_name, "rpc_start", s);
    FILE* rpc_end_file = generate_file_handle(method_name, "rpc_end", s);
    FILE* send_file = generate_file_handle(method_name, "c1_send", s);
    FILE* recv_file = generate_file_handle(method_name, "c1_recv", s);
    alloc_times = 0;
    read_times = 0;
    write_times = 0;
    free_times = 0;
    for (int i = 0; i < iterations; i++) {
      struct result res = test_add_arrays(client, s, targetIP, FALSE);
      alloc_times += res.alloc;
      read_times += res.read;
      write_times += res.write;
      free_times += res.free;
      fprintf(rpc_start_file, "%lu\n", res.rpc_start);
      fprintf(rpc_end_file, "%lu\n", res.rpc_end);
    }
    fprintf(alloc_file, "%d,%lu\n", s, alloc_times / (iterations*1000) );
    fprintf(read_file, "%d,%lu\n", s, read_times / (iterations*1000) );
    fprintf(write_file, "%d,%lu\n", s, write_times / (iterations*1000) );
    fprintf(free_file, "%d,%lu\n", s, free_times / (iterations*1000) );
    thrift_protocol_flush_timestamps(arrcomp_protocol, send_file, THRIFT_PERF_SEND, TRUE);
    thrift_protocol_flush_timestamps(arrcomp_protocol, recv_file, THRIFT_PERF_RECV, TRUE);
    fclose(send_file);
    fclose(recv_file);
    fclose(rpc_start_file);
    fclose(rpc_end_file);
  }
  fclose(alloc_file);
  fclose(read_file);
  fclose(write_file);
  fclose(free_file);
}

void test_shared_pointer_perf(RemoteMemTestIf *remmem_client, 
                              SimpleArrCompIf *arrcomp_client, 
                              struct sockaddr_in6 *targetIP, int iterations,
                              int max_size, int incr) {
  microbenchmark_perf(remmem_client, iterations);

  // TODO: debug, only the first one of these will work consistently, then the server seg faults
  // on a write_rmem. We might be running out of memory somewhere?

  printf("Starting no-op performance test...\n");
  // Call perf test for no-op RPC
  no_op_perf(arrcomp_client, targetIP, iterations, max_size, incr);

  thrift_protocol_flush_timestamps(arrcomp_protocol, NULL, THRIFT_PERF_SEND, FALSE);
  thrift_protocol_flush_timestamps(arrcomp_protocol, NULL, THRIFT_PERF_RECV, FALSE);

  printf("Starting increment array performance test...\n");
  // Call perf test for increment array rpc
  increment_array_perf(arrcomp_client, targetIP, iterations, max_size, incr, "incr_arr");

  thrift_protocol_flush_timestamps(arrcomp_protocol, NULL, THRIFT_PERF_SEND, FALSE);
  thrift_protocol_flush_timestamps(arrcomp_protocol, NULL, THRIFT_PERF_RECV, FALSE);

  printf("Starting add arrays performance test...\n");
  // Call perf test for add arrays
  add_arrays_perf(arrcomp_client, targetIP, iterations, max_size, incr, "add_arr");
}

int main (int argc, char *argv[]) {
  ThriftUDPSocket *remmem_socket;
  ThriftTransport *remmem_transport;
  RemoteMemTestIf *remmem_client;

  ThriftUDPSocket *arrcomp_socket;
  ThriftTransport *arrcomp_transport;
  SimpleArrCompIf *arrcomp_client;

  GError *error = NULL;

  int c; 
  struct config myConf;
  struct sockaddr_in6 *targetIP;
  int iterations = 0, max_size = BLOCK_SIZE, incr = 100;

  if (argc < 5) {
    usage(argv[0], "Not enough arguments");
    return -1;
  }

  while ((c = getopt (argc, argv, "c:i:m:s:")) != -1) { 
  switch (c) 
    { 
    case 'c':
      myConf = set_bb_config(optarg, 0);
      break;
    case 'i':
      iterations = atoi(optarg);
      break;
    case 'm':
      max_size = atoi(optarg);
      break;
    case 's':
      incr = atoi(optarg);
      break;
    case '?':
      usage(argv[0], "");
      return 0; 
    default: ; // b/c cannot create variable after label
      char *message = malloc(180);
      snprintf (message, 180, "Unknown option `-%c'.\n", optopt);
      usage(argv[0], message);
      free(message);
      return -1;
    } 
  }

  targetIP = init_sockets(&myConf, 0);
  set_host_list(myConf.hosts, myConf.num_hosts);

#if (!GLIB_CHECK_VERSION (2, 36, 0))
  g_type_init ();
#endif

  remmem_socket    = g_object_new (THRIFT_TYPE_UDP_SOCKET,
                            "hostname",  "102::",
                            "port",      9080,
                            NULL);
  remmem_transport = g_object_new (THRIFT_TYPE_BUFFERED_UDP_TRANSPORT,
                            "transport", remmem_socket,
                            NULL);
  remmem_protocol  = g_object_new (THRIFT_TYPE_BINARY_PROTOCOL,
                            "transport", remmem_transport,
                            NULL);

  thrift_transport_open (remmem_transport, &error);
  if (error) {
    printf ("ERROR: %s\n", error->message);
    g_clear_error (&error);
    return 1;
  }

  remmem_client = g_object_new (TYPE_REMOTE_MEM_TEST_CLIENT,
                                "input_protocol",  remmem_protocol,
                                "output_protocol", remmem_protocol,
                                NULL);
  
  arrcomp_socket    = g_object_new (THRIFT_TYPE_UDP_SOCKET,
                            "hostname",  "103::",
                            "port",      9180,
                            NULL);
  arrcomp_transport = g_object_new (THRIFT_TYPE_BUFFERED_UDP_TRANSPORT,
                            "transport", arrcomp_socket,
                            NULL);
  arrcomp_protocol  = g_object_new (THRIFT_TYPE_BINARY_PROTOCOL,
                            "transport", arrcomp_transport,
                            NULL);

  thrift_transport_open (arrcomp_transport, &error);
  if (error) {
    printf ("ERROR: %s\n", error->message);
    g_clear_error (&error);
    return 1;
  }

  arrcomp_client = g_object_new (TYPE_SIMPLE_ARR_COMP_CLIENT,
                                 "input_protocol",  arrcomp_protocol,
                                 "output_protocol", arrcomp_protocol,
                                 NULL);

  printf("\n\n###### Server functionality tests ######\n");
  test_server_functionality(remmem_client);

  printf("\n####### Shared pointer RPC tests #######\n");
  test_shared_pointer_rpc(arrcomp_client, targetIP);

  printf("\n####### Shared pointer performance tests #######\n");
  test_shared_pointer_perf(remmem_client, arrcomp_client, targetIP, iterations, max_size, incr);

  printf("\n\nCleaning up...\n");
  thrift_transport_close (remmem_transport, NULL);
  thrift_transport_close (arrcomp_transport, NULL);

  g_object_unref (remmem_client);
  g_object_unref (remmem_protocol);
  g_object_unref (remmem_transport);
  g_object_unref (remmem_socket);

  g_object_unref (arrcomp_client);
  g_object_unref (arrcomp_protocol);
  g_object_unref (arrcomp_transport);
  g_object_unref (arrcomp_socket);

  return 0;
}