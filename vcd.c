#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define USAGE "USAGE: vcd < in.vcd > out.ascii :\n"
#define PROLOG "Fatal error. Send your VCD at https://github.com/yne/vcd/issue"
#define die(...) exit(fprintf(stderr, PROLOG "\nReason: " __VA_ARGS__))

#define SFL 127             // scanf limit (for MAX_NAME)
#define MAX_NAME (SFL + 1)  // parsing token (channel name, scope name, ...)
#define MAX_SCOPE 32        // how many char scopes[] to allocate
#define MAX_CHANNEL 400     // how many Channel to allocate 96*96 = 8836
#define MAX_SAMPLE 2048     // how many Sample to allocate
#define ITV_TIME 10         // sample interval to display timestamp
#define VALUES "0123456789zZxXbU-"  // allowed bus values/types
#define COUNT(A) (sizeof(A) / sizeof(*A))
#define MAX(A, B) (A > B ? A : B)
#define VAL(A) #A
#define TXT(A) VAL(A)

typedef struct {
  char *low, *raise, *high, *drown, *start, *end;
  int skip;
} PrintOpt;
typedef struct {
  // could have as much state as it bus size but nobody handle such case
  // 'UZX' => show state
  // '\0' => show .val
  char state;
  unsigned val;
} Sample;
typedef struct {
  int size;
  int scope;
  char name[MAX_NAME];
  Sample samples[MAX_SAMPLE];
} Channel;

typedef struct {
  long long unsigned timestamps[MAX_SAMPLE];
  Channel ch[MAX_CHANNEL];           // [0] = timestamps
  char scopes[MAX_SCOPE][MAX_NAME];  //[0] = default
  int total, scope_count;
  double scale;  // duration of each sample
  char date[MAX_NAME], version[MAX_NAME], unit[MAX_NAME];  // file info
  // parsing related values
  int scope_cur;
  unsigned scope_lim, ch_lim, sz_lim;
} ParseCtx;

/* convert a base-94 chan id (!...~) to it integer equivalent (0...93) */
int chanId(char* str_id) {
  int id = 0;
  for (int i = strlen(str_id) - 1; i >= 0; i--) {
    id = (id * 94) + str_id[i] - '!';
  }
  if (id > MAX_CHANNEL) die("%s is over MAX_CHANNEL", str_id);
  return id;
}

int unilen(char* s) {
  int j = 0;
  for (; *s; s++) j += ((*s & 0xc0) != 0x80);
  return j;
}

/* read a $instruction and it opt if needed until $end*/
void parseVcdInstruction(ParseCtx* p) {
  char token[MAX_NAME];
  scanf("%" TXT(SFL) "s", token);
  if (!strcmp("var", token)) {
    char id[MAX_NAME];
    Channel c = {.scope = p->scope_cur};  // placeholder until id found
    scanf(" %*s %d %" TXT(SFL) "[^ ] %" TXT(SFL) "[^$]", &c.size, id, c.name);
    p->ch_lim = MAX(p->ch_lim, strlen(c.name));
    p->sz_lim = MAX(p->sz_lim, c.size);
    p->ch[chanId(id)] = c;
  } else if (!strcmp("scope", token)) {
    p->scope_count++;
    if (p->scope_count == MAX_SCOPE) {
      die("MAX_SCOPE reached\n");
    }
    p->scope_cur = p->scope_count;
    scanf("%*s %" TXT(SFL) "[^ $]", p->scopes[p->scope_cur]);
    p->scope_lim = MAX(p->scope_lim, strlen(p->scopes[p->scope_cur]));
  } else if (!strcmp("date", token)) {
    scanf("\n%" TXT(SFL) "[^$\n]", p->date);
  } else if (!strcmp("version", token)) {
    scanf("\n%" TXT(SFL) "[^$\n]", p->version);
  } else if (!strcmp("timescale", token)) {
    scanf("\n%lf%" TXT(SFL) "[^$\n]", &p->scale, p->unit);
  } else if (!strcmp("comment", token)) {
    scanf("\n%*[^$]");
  } else if (!strcmp("upscope", token)) {
    scanf("\n%*[^$]");
    p->scope_cur = 0;  // back to the root
  } else if (!strcmp("enddefinitions", token)) {
    scanf("\n%*[^$]");
  } else if (!strcmp("dumpvars", token)) {
  } else if (!strcmp("end", token)) {
  } else {
    printf("unknow token : %s\n", token);
  }
}
/* Parse a time line (ex: '#210000000') and copy all previous samples values */
void parseVcdTimestamp(ParseCtx* p) {
  if (p->total >= COUNT(p->ch[0].samples)) {
    die("MAX_SAMPLE reached");
  }
  // copy previous sample on every channel
  if (p->total > 0) {
    for (Channel* ch = p->ch; ch < p->ch + COUNT(p->ch); ch++) {
      ch->samples[p->total] = ch->samples[p->total - 1];
    }
  }
  scanf("%llu", &p->timestamps[p->total]);
  p->total++;
}
/*
sample line end with the channel ID and start either with a state or data:
state example: "1^" , "Z^"
data  example: "b0100 ^"
*/
void parseVcdSample(ParseCtx* p, int c) {
  Sample s = {'\0', 0};
  if (c == 'b') {
    for (c = getchar(); c != EOF && c != ' '; c = getchar()) {
      if (c == '0' || c == '1') {
        s.val = s.val * 2 + (c - '0');
      } else if (strchr(VALUES, c)) {
        s.state = c;
      } else {
        die("Unknown sample value: %c", c);
      }
    }
  } else {
    s.state = isalpha(c) ? c : '\0';
    s.val = isdigit(c) ? c - '0' : 0;
  }
  char id_str[MAX_NAME];
  scanf("%" TXT(SFL) "[^\n]", id_str);
  p->ch[chanId(id_str)].samples[p->total - 1] = s;
}

void parseVcd(ParseCtx* p) {
  for (int c = getchar(); c != EOF; c = getchar()) {
    if (isspace(c)) continue;
    if (c == '$') {
      parseVcdInstruction(p);
    } else if (c == '#') {
      parseVcdTimestamp(p);
    } else if (strchr(VALUES, c)) {
      parseVcdSample(p, c);
    } else {
      die("unknow char : %c\n", c);
    }
  }
}

void printYml(ParseCtx* p, PrintOpt* opt) {
  if (unilen(opt->high) != 1) die("high waveform length must be 1");
  if (unilen(opt->low) != 1) die("low waveform length must be 1");
  if (unilen(opt->drown) > 1) die("drown waveform length must be 1 or empty");
  if (unilen(opt->raise) > 1) die("raise waveform length must be 1 or empty");

  int zoom = (p->sz_lim + 7) >> 2;  // how many char per sample (8bit => 2)
  int trans = *opt->drown && *opt->raise;
  printf("global:\n");
  printf("  zoom: %i\n", zoom);
  printf("  date: %s\n", p->date);
  printf("  total: %i\n", p->total);
  printf("  skip: %i\n", opt->skip);
  printf("  time:\n");
  printf("    scale: %.2f\n", p->scale);
  printf("    unit: %s\n", p->unit ?: "?");
  printf("    %-*s: %s", p->ch_lim, "line", opt->start);
  for (double smpl = 0; smpl < p->total; smpl += ITV_TIME) {
    printf("%-*g ", ITV_TIME * zoom - 1, smpl * p->scale);
  }
  printf("%s\nchannels:\n", opt->end);
  for (Channel* ch = p->ch; ch - p->ch < COUNT(p->ch); ch++) {
    // skip empty ch
    if (!ch->size) continue;
    // print scope (if changed)
    if (ch == p->ch || ch->scope != ((ch - 1)->scope)) {
      printf("  %s:\n", ch->scope ? p->scopes[ch->scope] : "default");
    }

    printf("    %-*s: %s", p->ch_lim, ch->name, opt->start);
    for (Sample* s = ch->samples + opt->skip; s < ch->samples + p->total; s++) {
      Sample* prev = s > ch->samples ? s - 1 : s;
      if (s->state) {  // state data: UUUUZZZZ-
        printf("%-*c", zoom, s->state);
      } else if (ch->size == 1) {  // binary wave: ▁▁/▔▔
        // have a different data => print a transition
        for (int w = 0; w < zoom; w++) {
          if (!w && trans && s->state == prev->state && s->val != prev->val)
            printf("%s", prev->val ? opt->drown : opt->raise);
          else
            printf("%s", s->val ? opt->high : opt->low);
        }
      } else {  // bus : show hex value
        printf("%-*X", zoom, s->val);
      }
    }
    printf("%s\n", opt->end);
  }
}

int main(int argc, char** argv) {
  PrintOpt opt = {getenv("LOW") ?: "▁",       getenv("RAISE") ?: "╱",
                  getenv("HIGH") ?: "▔",      getenv("DROWN") ?: "╲",
                  getenv("STX") ?: "\"",      getenv("ETX") ?: "\"",
                  atoi(getenv("SKIP") ?: "0")};
  // PrintOpt opt = {"_", "/", "#", "\\"} {"▁", "╱", "▔", "╲"};
  ParseCtx ctx = {};
  parseVcd(&ctx);
  printYml(&ctx, &opt);
  return 0;
}
