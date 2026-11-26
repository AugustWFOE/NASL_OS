#ifndef HASHTABLE_H
#define HASHTABLE_H

#include <stdint.h>
#include <stddef.h>

typedef struct Node{
    uint64_t number;
    struct Node *next;    
}Node;

typedef struct{
    Node *head;
}Bucket;

typedef struct {
   Bucket *buckets;
   size_t buckets_count;
}HashTable;

int create_hashtable(HashTable *table, size_t bucket_count);
uint64_t *lookup(HashTable *table, uint8_t *hash);
int validate_output(uint8_t *hash, uint64_t answer);
int insert(HashTable *table, uint8_t *hash, uint64_t answer);
void destroy_hashtable(HashTable *table);

#endif

