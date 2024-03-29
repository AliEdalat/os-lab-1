// Console input and output.
// Input is from the keyboard or serial port.
// Output is written to the screen and serial port.

#include "types.h"
#include "defs.h"
#include "param.h"
#include "traps.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "fs.h"
#include "file.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "x86.h"

static void consputc(int);

static int panicked = 0;

static struct {
  struct spinlock lock;
  int locking;
} cons;

static void
printint(int xx, int base, int sign)
{
  static char digits[] = "0123456789abcdef";
  char buf[16];
  int i;
  uint x;

  if(sign && (sign = xx < 0))
    x = -xx;
  else
    x = xx;

  i = 0;
  do{
    buf[i++] = digits[x % base];
  }while((x /= base) != 0);

  if(sign)
    buf[i++] = '-';

  while(--i >= 0)
    consputc(buf[i]);
}
//PAGEBREAK: 50

// Print to the console. only understands %d, %x, %p, %s.
void
cprintf(char *fmt, ...)
{
  int i, c, locking;
  uint *argp;
  char *s;

  locking = cons.locking;
  if(locking)
    acquire(&cons.lock);

  if (fmt == 0)
    panic("null fmt");

  argp = (uint*)(void*)(&fmt + 1);
  for(i = 0; (c = fmt[i] & 0xff) != 0; i++){
    if(c != '%'){
      consputc(c);
      continue;
    }
    c = fmt[++i] & 0xff;
    if(c == 0)
      break;
    switch(c){
    case 'd':
      printint(*argp++, 10, 1);
      break;
    case 'x':
    case 'p':
      printint(*argp++, 16, 0);
      break;
    case 's':
      if((s = (char*)*argp++) == 0)
        s = "(null)";
      for(; *s; s++)
        consputc(*s);
      break;
    case '%':
      consputc('%');
      break;
    default:
      // Print unknown % sequence to draw attention.
      consputc('%');
      consputc(c);
      break;
    }
  }

  if(locking)
    release(&cons.lock);
}

void
panic(char *s)
{
  int i;
  uint pcs[10];

  cli();
  cons.locking = 0;
  // use lapiccpunum so that we can call panic from mycpu()
  cprintf("lapicid %d: panic: ", lapicid());
  cprintf(s);
  cprintf("\n");
  getcallerpcs(&s, pcs);
  for(i=0; i<10; i++)
    cprintf(" %p", pcs[i]);
  panicked = 1; // freeze other CPU
  for(;;)
    ;
}


#define BACKSPACE 0x100
#define LEFT 228
#define RIGHT 229
#define UP 226
#define DOWN 227
#define CRTPORT 0x3d4
#define BUFF_SIZE 80


static ushort *crt = (ushort*)P2V(0xb8000);  // CGA memory

static void
cgaputc(int c)
{
  int pos;

  // Cursor position: col + 80*row.
  outb(CRTPORT, 14);
  pos = inb(CRTPORT+1) << 8;
  outb(CRTPORT, 15);
  pos |= inb(CRTPORT+1);

  if(c == '\n') {
      pos += BUFF_SIZE - pos % BUFF_SIZE;
  } else if(c == BACKSPACE) {
      if (pos > 0){
          --pos;
          memmove(crt + pos, crt + pos + 1, sizeof(crt[0])*(24*BUFF_SIZE - pos));
      }
  } else if(c == LEFT){
      if (pos > 0)
          --pos;
  } else if(c == RIGHT){
      ++pos; /*if (pos < maximum_pos)*/
  } else if(c == UP || c == DOWN){
      // taking no action for UP or DOWN as is reasonable
  } else {
    memmove(crt + pos + 1, crt + pos, sizeof(crt[0]) * (24 * BUFF_SIZE - pos));
    crt[pos++] = (c & 0xff) | 0x0700;  // black on white
  }

  if(pos < 0 || pos > 25 * BUFF_SIZE)
    panic("pos under/overflow");

  if((pos/BUFF_SIZE) >= 24){  // Scroll up.
    memmove(crt, crt + BUFF_SIZE, sizeof(crt[0]) * 23 * BUFF_SIZE);
    pos -= BUFF_SIZE;
    memset(crt+pos, 0, sizeof(crt[0]) * (24 * BUFF_SIZE - pos));
  }

  outb(CRTPORT, 14);
  outb(CRTPORT + 1, pos >> 8);
  outb(CRTPORT, 15);
  outb(CRTPORT + 1, pos);
}

void
consputc(int c)
{
  if(panicked){
    cli();
    for(;;)
      ;
  }

  if(c == BACKSPACE){
    uartputc('\b'); uartputc(' '); uartputc('\b');
  } else if(c != UP && c != DOWN && c != LEFT && c != RIGHT){
    uartputc(c);
  }
  cgaputc(c);
}

#define INPUT_BUF 128
struct {
  char buf[INPUT_BUF];
  uint r;  // Read index
  uint w;  // Write index
  uint e;  // Edit index
  uint max;  // Maximum index
} input;

#define C(x)  ((x)-'@')  // Control-x

void
consoleintr(int (*getc)(void))
{
  int c, doprocdump = 0;

  acquire(&cons.lock);
  while((c = getc()) >= 0){
    switch(c){
    case C('P'):  // Process listing.
      // procdump() locks cons.lock indirectly; invoke later
      doprocdump = 1;
      break;
    case C('U'):  // Kill line.
      while(input.max != input.w &&
            input.buf[(input.max-1) % INPUT_BUF] != '\n'){
        input.max--;
	      input.e--;
        consputc(BACKSPACE);
      }
      break;
    case C('H'): case '\x7f':  // Backspace
      if(input.e != input.w){
        input.max--;
	      input.e--;
        memmove(input.buf + input.e, input.buf + input.e + 1, input.max - input.e); // TODO: check indices
        consputc(BACKSPACE);
      }
      break;
    case UP:
      consputc(c);
      break;
    case DOWN:
      consputc(c);
      break;
    case LEFT:
      if(input.e != input.w){
	      input.e--;
        consputc(c);
      }
      break;
    case RIGHT:
      if(input.e < input.max){
	      input.e++;
        consputc(c);
      }
      break;
    default:
      if(c != 0 && input.max < INPUT_BUF){
        c = (c == '\r') ? '\n' : c;
	if(c != '\n'){
	  memmove(input.buf + input.e + 1, input.buf + input.e, input.max - input.e);	
	  input.buf[input.e++ % INPUT_BUF] = c;
	  input.max++;
	  consputc(c);
	}
        else{
          input.buf[input.max++ % INPUT_BUF] = c;
          consputc(c);
        }
        if(c == '\n' || c == C('D') || input.max == input.r + INPUT_BUF){
          input.w = input.max;
	  input.e = input.max;
          wakeup(&input.r);
        }
      }
      break;
    }
  }
  release(&cons.lock);
  if(doprocdump) {
    procdump();  // now call procdump() wo. cons.lock held
  }
}

int
consoleread(struct inode *ip, char *dst, int n)
{
  uint target;
  int c;

  iunlock(ip);
  target = n;
  acquire(&cons.lock);
  while(n > 0){
    while(input.r == input.w){
      if(myproc()->killed){
        release(&cons.lock);
        ilock(ip);
        return -1;
      }
      sleep(&input.r, &cons.lock);
    }
    c = input.buf[input.r++ % INPUT_BUF];
    if(c == C('D')){  // EOF
      if(n < target){
        // Save ^D for next time, to make sure
        // caller gets a 0-byte result.
        input.r--;
      }
      break;
    }
    *dst++ = c;
    --n;
    if(c == '\n')
      break;
  }
  release(&cons.lock);
  ilock(ip);

  return target - n;
}

int
consolewrite(struct inode *ip, char *buf, int n)
{
  int i;

  iunlock(ip);
  acquire(&cons.lock);
  for(i = 0; i < n; i++)
    consputc(buf[i] & 0xff);
  release(&cons.lock);
  ilock(ip);

  return n;
}

void
consoleinit(void)
{
  initlock(&cons.lock, "console");

  devsw[CONSOLE].write = consolewrite;
  devsw[CONSOLE].read = consoleread;
  cons.locking = 1;

  ioapicenable(IRQ_KBD, 0);
}

