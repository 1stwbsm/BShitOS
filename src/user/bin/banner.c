/* banner: prints text in giant letters. */
#include <bshit/font5x7.h>

#include "bs.h"

#define GLYPH_HEIGHT 7
#define MAX_LETTERS 13

static void print_row(const char *text, unsigned int length, int row)
{
    for (unsigned int i = 0; i < length; i++) {
        unsigned char c = (unsigned char)text[i];
        const unsigned char *glyph = bs_font5x7[c >= 0x20 && c <= 0x7E ? c - 0x20 : 0];

        for (int column = 0; column < 5; column++)
            printf("%c", glyph[column] & (1 << row) ? '#' : ' ');
        printf(" ");
    }
    printf("\n");
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        printf("usage: banner text...\n");
        return 1;
    }
    for (int word = 1; word < argc; word++) {
        unsigned int length = strlen(argv[word]);

        if (length > MAX_LETTERS)
            length = MAX_LETTERS;
        for (int row = 0; row < GLYPH_HEIGHT; row++)
            print_row(argv[word], length, row);
        printf("\n");
    }
    return 0;
}
