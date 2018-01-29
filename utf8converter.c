#include <stdio.h>
#include <stdlib.h>
#include <iconv.h>
#include <errno.h>

#define U0000 0b00000000
#define U0080 0b11000000
#define U0800 0b11100000
#define U10000 0b11110000
#define U200000 0b11111000
#define U4000000 0b11111100

#define FIRST_TWO_BITS   0b10000000

#define SEVEN_BYTE_MASK 0b01111111
#define SIX_BYTE_MASK   0b00111111
#define FIVE_BYTE_MASK  0b00011111
#define FOUR_BYTE_MASK  0b00001111
#define THREE_BYTE_MASK 0b00000111
#define TWO_BYTE_MASK   0b00000011
#define ONE_BYTE_MASK   0b00000001

struct utf8 {
    char *bytes;
    int number_of_bytes;
};

void encode_struct(struct utf8 *utf, int bytes_num, int x) {
    utf->number_of_bytes = bytes_num;
    utf->bytes = (char *)calloc(bytes_num, sizeof(bytes_num));
    if(!utf->bytes) {
        perror("Calloc");
        return;
    }

    for(int i = bytes_num - 1; i > 0; --i) {
        utf->bytes[i] = (x & SIX_BYTE_MASK) | FIRST_TWO_BITS;
        x = x >> 6;
    }
}

struct utf8 convert_to_utf8(int x) {
    struct utf8 x_coded;
    int bytes_num = 0;

    if(x < 0) {
        x_coded.number_of_bytes = bytes_num;
        x_coded.bytes = NULL;
    } else if(x < 128) { // 7 bits for code point
        bytes_num = 1;
        encode_struct(&x_coded, bytes_num, x);
        x_coded.bytes[0] = (x & SEVEN_BYTE_MASK) | U0000;
    } else if(x < 2048) { // 11 bits for code point
        bytes_num = 2;
        encode_struct(&x_coded, bytes_num, x);
        x_coded.bytes[0] = ((x >> 6) & FIVE_BYTE_MASK) | U0080;
    } else if(x < 65536) { // 16 bits for code point
        bytes_num = 3;
        encode_struct(&x_coded, bytes_num, x);
        x_coded.bytes[0] = ((x >> 12) & FOUR_BYTE_MASK) | U0800;
    } else if(x < 1024*1024*2) { // 21 bits for code point
        bytes_num = 4;
        encode_struct(&x_coded, bytes_num, x);
        x_coded.bytes[0] = ((x >> 18) & THREE_BYTE_MASK) | U10000;
    } else if(x < 1024*1024*64) { // 26 bits for code point
        bytes_num = 5;
        encode_struct(&x_coded, bytes_num, x);
        x_coded.bytes[0] = ((x >> 24) & TWO_BYTE_MASK) | U200000;
    } else { // 31 bits for code point
       bytes_num = 6;
       encode_struct(&x_coded, bytes_num, x);
       x_coded.bytes[0] = ((x >> 30) & ONE_BYTE_MASK) | U4000000;
    }

    return x_coded;
}

void print_binary(char byte) {
    for(int i = 7; i >= 0; --i) {
        printf("%d", (byte >> i) & 0b00000001);
    }
    printf(" ");
}

void print_utf8_binary(struct utf8 x) {
    for(int i = 0; i < x.number_of_bytes; ++i) {
        print_binary(x.bytes[i]);
    }
    printf("\n");
}

int main()
{
    //  Powers of 2 testing
    const int buff_size = 30;
    int x[buff_size];
    x[0] = 1;

    for(int i = 1; i < buff_size + 1; ++i) {
        x[i] = x[i-1] * 2;
        printf("2**%d : ", i);
        print_utf8_binary(convert_to_utf8(x[i]));
    }

    return 0;
}
