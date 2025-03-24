#include <stdio.h>
#include <stdlib.h>

typedef unsigned char byte;

void func(int x)
{
    printf("%d\n", x);
}

int main(){
    //byte bitVector = 0b01010101;
    //printf("Original %d\n", bitVector);

    byte x = 0x13;
    byte proc = (x & 0xF0) >> 4;
    printf("Proc : %d\n", proc);
    func(proc);
    // byte mem = x & 0x0F;
    // printf("Mem : %d\n", mem);
    // int arr[4] = {10, 20, 30, 40};
    // printf("From proc %d\n", arr[proc]);
    // printf("From mem %d\n", arr[mem]);
    // printf("As int : %d\n", (int)proc);

    // int procNum = 7;
    // if (bitVector == 0x00){
    //     printf("Send message to node 0\n");
    // }else{
    //     while (bitVector != 0x00){
    //         if (bitVector & 1)
    //             printf("Send message to %d\n", procNum);
    //         bitVector >>= 1;
    //         procNum --;
    //     }
    // }
    // printf("Changed %d\n", bitVector);


    // printf("Original %d\n", bitVector);
    // int x = byte2Num(&bitVector);
    // printf("1st processor num = %d\n", x);
    // printf("Changed : %d\n", bitVector);
    // x = byte2Num(&bitVector);
    // printf("2nd Processor num = %d\n", x);

    // byte B = 0x15;
    // byte first = (B & 0xF0)>>4;
    // byte last = B & 0x0F;

    // byte cache = B % 4;
    // printf("Byte and Int\n");
    // printf("%02X and %d\n", B, (int)B);
    // printf("%02X and %d\n", first, (int)first);
    // printf("%02X and %d\n", last, (int)last);
    // printf("%02X and %d\n", cache, (int)cache);


}