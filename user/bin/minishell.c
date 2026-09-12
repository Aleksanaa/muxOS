// minishell.c - kernel minishell, no libc, no processes, no filesystem
#include "console.h"
#include "vga.h"

#define LINE_LEN 256
#define MAX_TOKENS 64
#define MAX_VARS 32
#define MAX_ARRAY_SIZE 64

// ---- minimal helpers (no libc) ----
static int k_isspace(char c) { return c == ' ' || c == '\t' || c == '\r'; }
static int k_isdigit(char c) { return c >= '0' && c <= '9'; }

static int k_strcmp(const char *a, const char *b) {
  while (*a && *a == *b) {
    a++;
    b++;
  }
  return (unsigned char)*a - (unsigned char)*b;
}
static void k_strcpy(char *d, const char *s) {
  while ((*d++ = *s++))
    ;
}
static void k_memcpy(char *d, const char *s, int n) {
  for (int i = 0; i < n; i++)
    d[i] = s[i];
}
static int k_atoi(const char *s) {
  int n = 0, neg = 0;
  if (*s == '-') {
    neg = 1;
    s++;
  }
  while (k_isdigit(*s))
    n = n * 10 + (*s++ - '0');
  return neg ? -n : n;
}
static void print_int(int val) {
  if (val == 0) {
    print("0", 0);
    return;
  }
  if (val < 0) {
    print("-", 0);
    val = -val;
  }
  char buf[12];
  int i = 11;
  buf[i] = '\0';
  while (val > 0) {
    buf[--i] = '0' + val % 10;
    val /= 10;
  }
  print(buf + i, 0);
}

// ---- token pool (replaces malloc) ----
// 每个嵌套层用独立的 pool，防止内层 tokenize 覆盖外层 tokens[]
#define MAX_DEPTH 6
static char tok_pools[MAX_DEPTH][MAX_TOKENS][LINE_LEN];
static int pool_depth = 0;
static char val_pool[MAX_ARRAY_SIZE][LINE_LEN]; // array-init 专用，独立于深度池

static void pool_set(char pool[][LINE_LEN], int cols, int idx, const char *s,
                     int len) {
  (void)cols;
  if (len > LINE_LEN - 1)
    len = LINE_LEN - 1;
  k_memcpy(pool[idx], s, len);
  pool[idx][len] = '\0';
}

// ---- data types ----
typedef struct {
  char name[32];
  int is_array;
  int value;
  int array[MAX_ARRAY_SIZE];
  int array_size;
} Var;

typedef enum { ADD = 1, SUB = -1, MUL = 2, DIV = 3, MOD = 4 } Operator;

static Var vars[MAX_VARS];
static int var_count = 0;

static Var *get_var(const char *name) {
  for (int i = 0; i < var_count; i++)
    if (k_strcmp(vars[i].name, name) == 0)
      return &vars[i];
  return 0;
}
static void set_var(const char *name, int value) {
  Var *v = get_var(name);
  if (v) {
    v->value = value;
    return;
  }
  if (var_count >= MAX_VARS) {
    print("Error: too many variables\n", 0);
    return;
  }
  k_strcpy(vars[var_count].name, name);
  vars[var_count].value = value;
  var_count++;
}
static void set_array(const char *name, int values[], int size) {
  Var *v = get_var(name);
  if (!v) {
    if (var_count >= MAX_VARS) {
      print("Error: too many variables\n", 0);
      return;
    }
    v = &vars[var_count++];
    k_strcpy(v->name, name);
  }
  v->is_array = 1;
  v->array_size = size;
  for (int i = 0; i < size; i++)
    v->array[i] = values[i];
}
static int get_array_element(const char *name, int index) {
  Var *v = get_var(name);
  if (!v || !v->is_array) {
    print("Error: not an array\n", 0);
    return 0;
  }
  if (index < 0 || index >= v->array_size) {
    print("Error: index out of bounds\n", 0);
    return 0;
  }
  return v->array[index];
}
static void set_array_element(const char *name, int index, int value) {
  Var *v = get_var(name);
  if (!v || !v->is_array) {
    print("Error: not an array\n", 0);
    return;
  }
  if (index < 0 || index >= v->array_size) {
    print("Error: index out of bounds\n", 0);
    return;
  }
  v->array[index] = value;
}

// ---- tokenizer ----
// pool must be char[MAX][64], val_pool variant is char[MAX][16]
static int tokenize_into(char *line, char *tokens[], char pool[][LINE_LEN]) {
  int count = 0;
  char *p = line;
  while (*p && count < MAX_TOKENS - 1) {
    if (k_isspace(*p)) {
      p++;
      continue;
    }

    // two-char operators
    if ((p[0] == '=' && p[1] == '=') || (p[0] == '!' && p[1] == '=') ||
        (p[0] == '>' && p[1] == '=') || (p[0] == '<' && p[1] == '=')) {
      pool_set(pool, LINE_LEN, count, p, 2);
      tokens[count] = pool[count];
      count++;
      p += 2;
      continue;
    }
    // single-char operators / punctuation
    if (*p == '>' || *p == '<' || *p == ';' || *p == '=' || *p == '+' ||
        *p == '-' || *p == '*' || *p == '/' || *p == '%' || *p == ',') {
      pool_set(pool, LINE_LEN, count, p, 1);
      tokens[count] = pool[count];
      count++;
      p++;
      continue;
    }
    // [ index ]
    if (*p == '[') {
      pool_set(pool, LINE_LEN, count, "[", 1);
      tokens[count] = pool[count];
      count++;
      p++;
      char *start = p;
      while (*p && *p != ']')
        p++;
      pool_set(pool, LINE_LEN, count, start, p - start);
      tokens[count] = pool[count];
      count++;
      if (*p == ']') {
        pool_set(pool, LINE_LEN, count, "]", 1);
        tokens[count] = pool[count];
        count++;
        p++;
      }
      continue;
    }
    // "string"
    if (*p == '"') {
      p++;
      char *start = p;
      while (*p && *p != '"')
        p++;
      pool_set(pool, LINE_LEN, count, start, p - start);
      tokens[count] = pool[count];
      count++;
      if (*p == '"')
        p++;
      continue;
    }
    // { body }
    if (*p == '{') {
      pool_set(pool, LINE_LEN, count, "{", 1);
      tokens[count] = pool[count];
      count++;
      p++;
      char *start = p;
      int depth = 1;
      while (*p && depth > 0) {
        if (*p == '{')
          depth++;
        else if (*p == '}')
          depth--;
        if (depth > 0)
          p++;
      }
      pool_set(pool, LINE_LEN, count, start, p - start);
      tokens[count] = pool[count];
      count++;
      pool_set(pool, LINE_LEN, count, "}", 1);
      tokens[count] = pool[count];
      count++;
      if (*p == '}')
        p++;
      continue;
    }
    // word / number
    {
      char *start = p;
      while (*p && !k_isspace(*p) && *p != '<' && *p != '>' && *p != '!' &&
             *p != ',' && *p != '[' && *p != ']' && *p != '{' && *p != '}' &&
             *p != ';' && *p != '=' && *p != '*' && *p != '/' && *p != '%' &&
             *p != '+' && *p != '-')
        p++;
      if (p == start) {
        p++;
        continue;
      } // skip unknown char
      pool_set(pool, LINE_LEN, count, start, p - start);
      tokens[count] = pool[count];
      count++;
    }
  }
  return count;
}

static int tokenize(char *line, char *tokens[]) {
  return tokenize_into(line, tokens, tok_pools[pool_depth]);
}

// ---- forward decl ----
void handle_line(char *line);

static int is_number(const char *s) {
  if (!*s)
    return 0;
  while (*s) {
    if (!k_isdigit(*s))
      return 0;
    s++;
  }
  return 1;
}

static int eval_expr(char *tokens[], int count) {
  int result = 0, sign = ADD;
  for (int i = 0; i < count; i++) {
    char *t = tokens[i];
    if (k_strcmp(t, "+") == 0) {
      sign = ADD;
      continue;
    }
    if (k_strcmp(t, "-") == 0) {
      sign = SUB;
      continue;
    }
    if (k_strcmp(t, "*") == 0) {
      sign = MUL;
      continue;
    }
    if (k_strcmp(t, "/") == 0) {
      sign = DIV;
      continue;
    }
    if (k_strcmp(t, "%") == 0) {
      sign = MOD;
      continue;
    }
    int val = is_number(t) ? k_atoi(t) : (get_var(t) ? get_var(t)->value : 0);
    if (sign == ADD)
      result += val;
    else if (sign == SUB)
      result -= val;
    else if (sign == MUL)
      result *= val;
    else if (sign == DIV) {
      if (!val) {
        print("Error: div by zero\n", 0);
        return 0;
      }
      result /= val;
    } else if (sign == MOD) {
      if (!val) {
        print("Error: div by zero\n", 0);
        return 0;
      }
      result %= val;
    }
  }
  return result;
}

static int eval_cond(int l, const char *op, int r) {
  if (k_strcmp(op, "==") == 0)
    return l == r;
  if (k_strcmp(op, "!=") == 0)
    return l != r;
  if (k_strcmp(op, ">") == 0)
    return l > r;
  if (k_strcmp(op, "<") == 0)
    return l < r;
  if (k_strcmp(op, ">=") == 0)
    return l >= r;
  if (k_strcmp(op, "<=") == 0)
    return l <= r;
  return 0;
}

void execute(char *tokens[], int count) {
  if (count == 0)
    return;

  // x = arr [ i ]
  if (count >= 6 && k_strcmp(tokens[1], "=") == 0 &&
      k_strcmp(tokens[3], "[") == 0 && k_strcmp(tokens[5], "]") == 0) {
    int idx = is_number(tokens[4])
                  ? k_atoi(tokens[4])
                  : (get_var(tokens[4]) ? get_var(tokens[4])->value : 0);
    set_var(tokens[0], get_array_element(tokens[2], idx));
    return;
  }
  // arr [ i ] = val  (no { })
  if (count >= 6 && k_strcmp(tokens[1], "[") == 0 &&
      k_strcmp(tokens[3], "]") == 0 && k_strcmp(tokens[4], "=") == 0 &&
      k_strcmp(tokens[5], "{") != 0) {
    int idx = is_number(tokens[2])
                  ? k_atoi(tokens[2])
                  : (get_var(tokens[2]) ? get_var(tokens[2])->value : 0);
    int val = is_number(tokens[5])
                  ? k_atoi(tokens[5])
                  : (get_var(tokens[5]) ? get_var(tokens[5])->value : 0);
    set_array_element(tokens[0], idx, val);
    return;
  }
  // arr [ size ] = { 1, 2, 3 }
  if (count >= 8 && k_strcmp(tokens[1], "[") == 0 &&
      k_strcmp(tokens[3], "]") == 0 && k_strcmp(tokens[4], "=") == 0 &&
      k_strcmp(tokens[5], "{") == 0) {
    // save what we need before inner tokenize overwrites tok_pool
    char arr_name[32];
    k_strcpy(arr_name, tokens[0]);
    int array_size = is_number(tokens[2])
                         ? k_atoi(tokens[2])
                         : (get_var(tokens[2]) ? get_var(tokens[2])->value : 0);
    char body[128];
    k_strcpy(body, tokens[6]);

    // tokenize body into val_pool (separate pool, won't collide)
    char *vt[MAX_ARRAY_SIZE];
    int vc = tokenize_into(body, vt, val_pool);
    int values[MAX_ARRAY_SIZE] = {0}, size = 0;
    for (int i = 0; i < vc; i++)
      if (k_strcmp(vt[i], ",") != 0)
        values[size++] = k_atoi(vt[i]);
    if (size > array_size) {
      print("Error: too many initializers\n", 0);
      return;
    }
    set_array(arr_name, values, array_size);
    return;
  }
  // echo var / echo str
  if (count == 2 && k_strcmp(tokens[0], "echo") == 0) {
    Var *v = get_var(tokens[1]);
    if (v) {
      print_int(v->value);
      print("\n", 0);
    } else {
      print(tokens[1], 0);
      print("\n", 0);
    }
    return;
  }
  // echo arr [ i ]
  if (count >= 5 && k_strcmp(tokens[0], "echo") == 0 &&
      k_strcmp(tokens[2], "[") == 0 && k_strcmp(tokens[4], "]") == 0) {
    int idx = is_number(tokens[3])
                  ? k_atoi(tokens[3])
                  : (get_var(tokens[3]) ? get_var(tokens[3])->value : 0);
    print_int(get_array_element(tokens[1], idx));
    print("\n", 0);
    return;
  }
  // while cond { body }
  // 必须先把 tokens 内容复制到本地 —— handle_line 会覆盖全局 tok_pool，
  // 导致第二次循环时 tokens[1/2/3/5] 全被污染
  if (count >= 6 && k_strcmp(tokens[0], "while") == 0) {
    char lhs[64], op[4], rhs[64], body[LINE_LEN];
    k_strcpy(lhs, tokens[1]);
    k_strcpy(op, tokens[2]);
    k_strcpy(rhs, tokens[3]);
    k_strcpy(body, tokens[5]);
    char *lp[1] = {lhs}, *rp[1] = {rhs};
    int limit = 100000;
    while (limit-- > 0) {
      if (!eval_cond(eval_expr(lp, 1), op, eval_expr(rp, 1)))
        break;
      handle_line(body);
    }
    if (limit <= 0)
      print("Error: loop limit reached\n", 0);
    return;
  }
  // if cond { body }
  if (count >= 6 && k_strcmp(tokens[0], "if") == 0) {
    char lhs[64], op[4], rhs[64], body[LINE_LEN];
    k_strcpy(lhs, tokens[1]);
    k_strcpy(op, tokens[2]);
    k_strcpy(rhs, tokens[3]);
    k_strcpy(body, tokens[5]);
    char *lp[1] = {lhs}, *rp[1] = {rhs};
    if (eval_cond(eval_expr(lp, 1), op, eval_expr(rp, 1)))
      handle_line(body);
    return;
  }
  // x = expr  (arithmetic)
  if (count >= 3 && k_strcmp(tokens[1], "=") == 0) {
    set_var(tokens[0], eval_expr(&tokens[2], count - 2));
    return;
  }
  // unknown command
  if (count >= 1) {
    print("Unknown: ", 0);
    print(tokens[0], 0);
    print("\n", 0);
  }
}

void handle_line(char *line) {
  if (pool_depth >= MAX_DEPTH) {
    print("Error: nesting too deep\n", 0);
    return;
  }
  char *tokens[MAX_TOKENS];
  int depth = pool_depth++; // 占用当前层，深度递增
  int count = tokenize_into(line, tokens, tok_pools[depth]);
  if (count == 0) {
    pool_depth--;
    return;
  }

  if (k_strcmp(tokens[0], "if") == 0 || k_strcmp(tokens[0], "while") == 0) {
    int end = count;
    for (int i = 0; i < count; i++)
      if (k_strcmp(tokens[i], "}") == 0) {
        end = i + 1;
        break;
      }
    execute(tokens, end);
    // handle trailing ; statements
    if (end < count && k_strcmp(tokens[end], ";") == 0) {
      int start = ++end;
      for (int i = end; i < count; i++) {
        if (k_strcmp(tokens[i], ";") == 0) {
          execute(&tokens[start], i - start);
          start = i + 1;
        }
      }
    }
    pool_depth--;
    return;
  }

  int start = 0;
  for (int i = 0; i < count; i++) {
    if (k_strcmp(tokens[i], ";") == 0) {
      execute(&tokens[start], i - start);
      start = i + 1;
    } else if (i == count - 1) {
      print("Error: missing ';'\n", 0);
      pool_depth--;
      return;
    }
  }
  pool_depth--;
}

void minishell_run(void) {
  char line[LINE_LEN];
  print("mini-shell (type 'exit' to quit)\n", 0x0B);
  while (1) {
    print("mini-sh> ", 0x0A);
    readline(line, LINE_LEN);
    if (k_strcmp(line, "exit") == 0)
      break;
    if (line[0] == '\0')
      continue;
    handle_line(line);
  }
  print("Bye\n", 0);
}
