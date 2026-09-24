#ifndef HW_H
#define HW_H

/* Memory-mapped register access. Hardware registers must be volatile so the
   compiler does not fold repeated reads (status polling) away. */
#define HW(a) (*(volatile char *)(a))

/* CIA #1 keyboard ports ($dc00 = column select, $dc01 = row read). */
#define CIA1_PRA HW(0xdc00)
#define CIA1_PRB HW(0xdc01)

/* Both player bars are steered from the keyboard matrix via keys(). */
#define KEY_P1_UP 1
#define KEY_P1_DOWN 2
#define KEY_P2_UP 4
#define KEY_P2_DOWN 8
#define KEY_QUIT 16

#endif /* HW_H */
