#include "hashtable.h"
#include "lonesha256.h"
#include "messages.h"
#include <stdlib.h>
#include <string.h>
#include <endian.h>


/*
create_hashtable(HashTable *table, size_t bucket_count)
    -simply allocates memory to the hashtable and initialiser the correct values.
     imagine its the constructor for the hashtable.

hash_function(HashTable *table, uint8_t *hash)
    -loops through the hash array and takes modulo for each index
     and added the rest to the next index.

*lookup(HashTable *table, uint8_t *hash)
    -returns a address for the answer we are looking for or NULL if it doesn't exist.

validate_output(uint8_t *hash, uint64_t answer)
    -does the same as the server. Convertes the answer to little endian and
     gets the hash from lonesha256, where we validates its the same.
     
insert(HashTable *table, uint8_t *hash, uint64_t answer)
    -allocate memory for the new Node and addes it at the front of the link-list.
*/


int create_hashtable(HashTable *table, size_t bucket_count){

    //validate if we have a hashtable and that we have more than 0 buckets in it.
    if(table == NULL || bucket_count == 0){
        return -1;
    }

    //alucates memory for all the buckets.
    table->buckets = calloc(bucket_count, sizeof(Bucket));

    if(table->buckets == NULL){
        return -1;
    }

    table->buckets_count = bucket_count;

    return 0;
}

static uint64_t hash_function(HashTable *table, uint8_t *hash){
    //hash funktion
    uint64_t bucket_index = 0;

    //takes modulo of all the bytes in the hash
    for(int i = 0; i<32; i++){
        bucket_index = ((bucket_index * 256) + hash[i]) % table->buckets_count;
    }

    return bucket_index;
}

uint64_t *lookup(HashTable *table, uint8_t *hash){

    uint64_t bucket_index = hash_function(table, hash);

    Node *nextNode = table->buckets[bucket_index].head;

    //no bucket with the answer
    if(nextNode == NULL){
        return NULL;
    }

    while (nextNode != NULL)
    {
        if(validate_output(hash, nextNode->number) == 0){
            return &nextNode->number;
        }
        nextNode = nextNode->next;
    }
   
    return NULL;
}

int validate_output(uint8_t *hash, uint64_t answer){

    uint64_t output = htole64(answer);
    uint8_t validate_hash[32];
    lonesha256(validate_hash, (const unsigned char *)&output, sizeof(output));
    if (memcmp(validate_hash, hash, 32) == 0) {
        return 0;
    }
    return -1;
}

int insert(HashTable *table, uint8_t *hash, uint64_t answer){
    
    //Should be used if we want to make sure that we can't get dublicates
    //or we don't catch raceconditions.

    // if(lookup(table, hash) != NULL){
    //     return -1;
    // }

    //Creates a new Node with the new answer values
    Node *new_input = malloc(sizeof(Node));
    if(new_input == NULL){
        return -1;
    }
    new_input->next = NULL;
    new_input->number = answer;

    //hash funktion
    uint64_t bucket_index = hash_function(table, hash);

    //finding head of linklist and updates the head
    Node *nextNode = table->buckets[bucket_index].head;
    if(nextNode != NULL){

        new_input->next = table->buckets[bucket_index].head;
        table->buckets[bucket_index].head = new_input;

    }else{
        table->buckets[bucket_index].head = new_input;
    }

    return 0;
}
void clear_hashtable(HashTable *table)
{
    if (table == NULL || table->buckets == NULL)
        return;

    for (size_t i = 0; i < table->buckets_count; i++)
    {
        Node *current = table->buckets[i].head;

        while (current != NULL)
        {
            Node *next = current->next;
            free(current);
            current = next;
        }

        table->buckets[i].head = NULL;
    }
}
void destroy_hashtable(HashTable *table)
{
    if (table == NULL)
        return;

    clear_hashtable(table);

    free(table->buckets);

    table->buckets = NULL;
    table->buckets_count = 0;
}