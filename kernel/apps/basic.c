/*
 * MiniOS BASIC Interpreter
 *
 * A simple BASIC interpreter supporting classic commands:
 * - PRINT, INPUT, LET
 * - IF/THEN, GOTO, GOSUB/RETURN
 * - FOR/NEXT
 * - RUN, LIST, NEW, SAVE, LOAD
 * - END, REM
 */

#include "../include/basic.h"
#include "../include/vga.h"
#include "../include/keyboard.h"
#include "../include/string.h"
#include "../include/stdio.h"
#include "../include/heap.h"
#include "../include/vfs.h"
#include "../include/ramfs.h"
#include "../include/ext2.h"
#include "../include/shell.h"

/* BASIC configuration */
#define BASIC_MAX_LINES     500
#define BASIC_MAX_LINE_LEN  128
#define BASIC_MAX_VARS      52      /* A-Z and a-z */
#define BASIC_MAX_STRINGS   26      /* A$-Z$ */
#define BASIC_STRING_LEN    128
#define BASIC_STACK_SIZE    32      /* GOSUB/FOR stack */
#define BASIC_MAX_LINE_NUM  9999

/* Program line structure */
typedef struct {
    int line_num;
    char text[BASIC_MAX_LINE_LEN];
} basic_line_t;

/* FOR loop state */
typedef struct {
    char var;           /* Loop variable */
    int target;         /* Target value */
    int step;           /* Step value */
    int return_line;    /* Line to return to */
} for_state_t;

/* Interpreter state */
static basic_line_t program[BASIC_MAX_LINES];
static int num_lines = 0;
static double variables[BASIC_MAX_VARS];       /* Numeric variables A-Z, a-z */
static char strings[BASIC_MAX_STRINGS][BASIC_STRING_LEN];  /* String variables A$-Z$ */
static int gosub_stack[BASIC_STACK_SIZE];
static int gosub_sp = 0;
static for_state_t for_stack[BASIC_STACK_SIZE];
static int for_sp = 0;
static bool running = false;
static int current_line_idx = 0;
static char input_buffer[BASIC_MAX_LINE_LEN];
static char filename[64] = {0};

/* Forward declarations */
static void basic_execute_line(const char* line);
static double basic_eval_expr(const char** p);
static void basic_skip_spaces(const char** p);
static int basic_find_line(int line_num);
static void basic_insert_line(int line_num, const char* text);
static void basic_delete_line(int line_num);
static void basic_list(int start, int end);
static void basic_run_program(void);
static void basic_new(void);
static void basic_save(const char* fname);
static void basic_load(const char* fname);
static int basic_get_var_index(char c);

/*
 * Get input line from keyboard
 */
static int basic_getline(char* buf, int maxlen) {
    int pos = 0;
    
    while (pos < maxlen - 1) {
        char c = keyboard_getchar();
        
        if (c == '\n') {
            buf[pos] = '\0';
            vga_putchar('\n');
            return pos;
        } else if (c == '\b') {
            if (pos > 0) {
                pos--;
                vga_putchar('\b');
            }
        } else if (c >= 32 && c < 127) {
            buf[pos++] = c;
            vga_putchar(c);
        }
    }
    
    buf[pos] = '\0';
    return pos;
}

/*
 * Skip whitespace
 */
static void basic_skip_spaces(const char** p) {
    while (**p == ' ' || **p == '\t') (*p)++;
}

/*
 * Get variable index (0-51 for A-Z, a-z)
 */
static int basic_get_var_index(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return 26 + (c - 'a');
    return -1;
}

/*
 * Parse a number
 */
static double basic_parse_number(const char** p) {
    double result = 0;
    double fraction = 0;
    double divisor = 10;
    int sign = 1;
    
    basic_skip_spaces(p);
    
    if (**p == '-') {
        sign = -1;
        (*p)++;
    } else if (**p == '+') {
        (*p)++;
    }
    
    while (**p >= '0' && **p <= '9') {
        result = result * 10 + (**p - '0');
        (*p)++;
    }
    
    if (**p == '.') {
        (*p)++;
        while (**p >= '0' && **p <= '9') {
            fraction += (**p - '0') / divisor;
            divisor *= 10;
            (*p)++;
        }
        result += fraction;
    }
    
    return sign * result;
}

/*
 * Parse a factor (number, variable, or parenthesized expression)
 */
static double basic_parse_factor(const char** p) {
    basic_skip_spaces(p);
    
    if (**p == '(') {
        (*p)++;
        double result = basic_eval_expr(p);
        basic_skip_spaces(p);
        if (**p == ')') (*p)++;
        return result;
    }
    
    /* Variable */
    if ((**p >= 'A' && **p <= 'Z') || (**p >= 'a' && **p <= 'z')) {
        char c = **p;
        (*p)++;
        /* Check if it's a string variable (followed by $) */
        if (**p == '$') {
            (*p)++;
            return 0;  /* String variables return 0 in numeric context */
        }
        int idx = basic_get_var_index(c);
        if (idx >= 0) return variables[idx];
        return 0;
    }
    
    /* Number */
    if ((**p >= '0' && **p <= '9') || **p == '.' || **p == '-') {
        return basic_parse_number(p);
    }
    
    /* Built-in functions */
    if (strncmp(*p, "RND", 3) == 0) {
        (*p) += 3;
        basic_skip_spaces(p);
        if (**p == '(') {
            (*p)++;
            basic_eval_expr(p);  /* Consume argument */
            if (**p == ')') (*p)++;
        }
        /* Simple pseudo-random using timer */
        static uint32_t seed = 12345;
        seed = seed * 1103515245 + 12345;
        return (double)(seed % 32768) / 32768.0;
    }
    
    if (strncmp(*p, "ABS", 3) == 0) {
        (*p) += 3;
        basic_skip_spaces(p);
        if (**p == '(') {
            (*p)++;
            double val = basic_eval_expr(p);
            if (**p == ')') (*p)++;
            return val < 0 ? -val : val;
        }
    }
    
    if (strncmp(*p, "INT", 3) == 0) {
        (*p) += 3;
        basic_skip_spaces(p);
        if (**p == '(') {
            (*p)++;
            double val = basic_eval_expr(p);
            if (**p == ')') (*p)++;
            return (int)val;
        }
    }
    
    return 0;
}

/*
 * Parse term (handles * and /)
 */
static double basic_parse_term(const char** p) {
    double result = basic_parse_factor(p);
    
    while (1) {
        basic_skip_spaces(p);
        if (**p == '*') {
            (*p)++;
            result *= basic_parse_factor(p);
        } else if (**p == '/') {
            (*p)++;
            double divisor = basic_parse_factor(p);
            if (divisor != 0) result /= divisor;
        } else {
            break;
        }
    }
    
    return result;
}

/*
 * Evaluate expression (handles + and -)
 */
static double basic_eval_expr(const char** p) {
    basic_skip_spaces(p);
    double result = basic_parse_term(p);
    
    while (1) {
        basic_skip_spaces(p);
        if (**p == '+') {
            (*p)++;
            result += basic_parse_term(p);
        } else if (**p == '-') {
            (*p)++;
            result -= basic_parse_term(p);
        } else {
            break;
        }
    }
    
    return result;
}

/*
 * Evaluate comparison
 */
static int basic_eval_comparison(const char** p) {
    double left = basic_eval_expr(p);
    basic_skip_spaces(p);
    
    char op1 = **p;
    char op2 = 0;
    (*p)++;
    if (**p == '=' || **p == '>') {
        op2 = **p;
        (*p)++;
    }
    
    double right = basic_eval_expr(p);
    
    if (op1 == '=' && op2 == 0) return left == right;
    if (op1 == '<' && op2 == 0) return left < right;
    if (op1 == '>' && op2 == 0) return left > right;
    if (op1 == '<' && op2 == '=') return left <= right;
    if (op1 == '>' && op2 == '=') return left >= right;
    if (op1 == '<' && op2 == '>') return left != right;
    if (op1 == '!' && op2 == '=') return left != right;
    
    return 0;
}

/*
 * Find line by line number
 */
static int basic_find_line(int line_num) {
    for (int i = 0; i < num_lines; i++) {
        if (program[i].line_num == line_num) return i;
    }
    return -1;
}

/*
 * Find line index >= line_num
 */
static int basic_find_line_ge(int line_num) {
    for (int i = 0; i < num_lines; i++) {
        if (program[i].line_num >= line_num) return i;
    }
    return -1;
}

/*
 * Insert or replace a line
 */
static void basic_insert_line(int line_num, const char* text) {
    /* Check if line exists */
    int idx = basic_find_line(line_num);
    if (idx >= 0) {
        /* Replace existing line */
        strncpy(program[idx].text, text, BASIC_MAX_LINE_LEN - 1);
        program[idx].text[BASIC_MAX_LINE_LEN - 1] = '\0';
        return;
    }
    
    if (num_lines >= BASIC_MAX_LINES) {
        vga_puts("?OUT OF MEMORY\n");
        return;
    }
    
    /* Find insertion point */
    int insert_at = num_lines;
    for (int i = 0; i < num_lines; i++) {
        if (program[i].line_num > line_num) {
            insert_at = i;
            break;
        }
    }
    
    /* Shift lines down */
    for (int i = num_lines; i > insert_at; i--) {
        program[i] = program[i - 1];
    }
    
    /* Insert new line */
    program[insert_at].line_num = line_num;
    strncpy(program[insert_at].text, text, BASIC_MAX_LINE_LEN - 1);
    program[insert_at].text[BASIC_MAX_LINE_LEN - 1] = '\0';
    num_lines++;
}

/*
 * Delete a line
 */
static void basic_delete_line(int line_num) {
    int idx = basic_find_line(line_num);
    if (idx < 0) return;
    
    /* Shift lines up */
    for (int i = idx; i < num_lines - 1; i++) {
        program[i] = program[i + 1];
    }
    num_lines--;
}

/*
 * LIST command
 */
static void basic_list(int start, int end) {
    for (int i = 0; i < num_lines; i++) {
        if (program[i].line_num >= start && program[i].line_num <= end) {
            char buf[16];
            snprintf(buf, sizeof(buf), "%d ", program[i].line_num);
            vga_puts(buf);
            vga_puts(program[i].text);
            vga_putchar('\n');
        }
    }
}

/*
 * NEW command - clear program
 */
static void basic_new(void) {
    num_lines = 0;
    gosub_sp = 0;
    for_sp = 0;
    for (int i = 0; i < BASIC_MAX_VARS; i++) variables[i] = 0;
    for (int i = 0; i < BASIC_MAX_STRINGS; i++) strings[i][0] = '\0';
    filename[0] = '\0';
}

/*
 * SAVE command
 */
static void basic_save(const char* fname) {
    char resolved[256];
    shell_resolve_path(fname, resolved, sizeof(resolved));
    
    /* Build content */
    char* content = (char*)kmalloc(BASIC_MAX_LINES * BASIC_MAX_LINE_LEN);
    if (!content) {
        vga_puts("?OUT OF MEMORY\n");
        return;
    }
    
    int pos = 0;
    for (int i = 0; i < num_lines; i++) {
        pos += snprintf(content + pos, BASIC_MAX_LINE_LEN + 16, 
                       "%d %s\n", program[i].line_num, program[i].text);
    }
    
    /* Create or overwrite file */
    vfs_node_t* file = vfs_lookup(resolved);
    if (file) {
        file->length = 0;
    } else {
        vfs_node_t* cwd = shell_get_cwd_node();
        if (cwd->readdir == ext2_vfs_readdir) {
            file = ext2_create_file(cwd, fname);
        } else {
            file = ramfs_create_file_in(cwd, fname, 0);
        }
    }
    
    if (!file) {
        vga_puts("?FILE ERROR\n");
        kfree(content);
        return;
    }
    
    vfs_write(file, 0, pos, (uint8_t*)content);
    kfree(content);
    
    strncpy(filename, fname, sizeof(filename) - 1);
    vga_puts("OK\n");
}

/*
 * LOAD command
 */
static void basic_load(const char* fname) {
    char resolved[256];
    shell_resolve_path(fname, resolved, sizeof(resolved));
    
    vfs_node_t* file = vfs_lookup(resolved);
    if (!file) {
        vga_puts("?FILE NOT FOUND\n");
        return;
    }
    
    if (file->length == 0) {
        vga_puts("?EMPTY FILE\n");
        return;
    }
    
    char* content = (char*)kmalloc(file->length + 1);
    if (!content) {
        vga_puts("?OUT OF MEMORY\n");
        return;
    }
    
    vfs_read(file, 0, file->length, (uint8_t*)content);
    content[file->length] = '\0';
    
    /* Clear current program */
    basic_new();
    
    /* Parse lines */
    char* line = content;
    while (*line) {
        /* Find end of line */
        char* eol = line;
        while (*eol && *eol != '\n') eol++;
        char saved = *eol;
        *eol = '\0';
        
        /* Parse line number */
        if (*line >= '0' && *line <= '9') {
            int line_num = atoi(line);
            /* Skip to text */
            while (*line >= '0' && *line <= '9') line++;
            while (*line == ' ') line++;
            basic_insert_line(line_num, line);
        }
        
        *eol = saved;
        line = (*eol) ? eol + 1 : eol;
    }
    
    kfree(content);
    strncpy(filename, fname, sizeof(filename) - 1);
    vga_puts("OK\n");
}

/*
 * Execute a single BASIC statement
 */
static void basic_execute_statement(const char* stmt) {
    const char* p = stmt;
    basic_skip_spaces(&p);
    
    /* REM - comment */
    if (strncmp(p, "REM", 3) == 0) {
        return;
    }
    
    /* PRINT */
    if (strncmp(p, "PRINT", 5) == 0) {
        p += 5;
        basic_skip_spaces(&p);
        
        while (*p && *p != ':') {
            if (*p == '"') {
                /* String literal */
                p++;
                while (*p && *p != '"') {
                    vga_putchar(*p++);
                }
                if (*p == '"') p++;
            } else if (*p == ';') {
                p++;  /* No newline, continue */
            } else if (*p == ',') {
                vga_puts("\t");
                p++;
            } else {
                /* Expression */
                double val = basic_eval_expr(&p);
                char buf[32];
                int ival = (int)val;
                if (val == ival) {
                    snprintf(buf, sizeof(buf), "%d", ival);
                } else {
                    snprintf(buf, sizeof(buf), "%d.%02d", ival, (int)((val - ival) * 100));
                }
                vga_puts(buf);
            }
            basic_skip_spaces(&p);
        }
        
        /* Check if line ends with semicolon */
        const char* end = stmt + strlen(stmt) - 1;
        while (end > stmt && (*end == ' ' || *end == '\t')) end--;
        if (*end != ';') {
            vga_putchar('\n');
        }
        return;
    }
    
    /* INPUT */
    if (strncmp(p, "INPUT", 5) == 0) {
        p += 5;
        basic_skip_spaces(&p);
        
        /* Optional prompt */
        if (*p == '"') {
            p++;
            while (*p && *p != '"') {
                vga_putchar(*p++);
            }
            if (*p == '"') p++;
            basic_skip_spaces(&p);
            if (*p == ';' || *p == ',') p++;
            basic_skip_spaces(&p);
        } else {
            vga_puts("? ");
        }
        
        /* Get variable */
        if ((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z')) {
            char var = *p++;
            bool is_string = (*p == '$');
            if (is_string) p++;
            
            char input[64];
            basic_getline(input, sizeof(input));
            
            if (is_string) {
                int idx = basic_get_var_index(var);
                if (idx >= 0 && idx < BASIC_MAX_STRINGS) {
                    strncpy(strings[idx], input, BASIC_STRING_LEN - 1);
                }
            } else {
                int idx = basic_get_var_index(var);
                if (idx >= 0) {
                    variables[idx] = atoi(input);
                }
            }
        }
        return;
    }
    
    /* LET (optional keyword) - explicit assignment */
    if (strncmp(p, "LET", 3) == 0) {
        p += 3;
        basic_skip_spaces(&p);
        
        if ((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z')) {
            char var = *p++;
            bool is_string = (*p == '$');
            if (is_string) p++;
            
            basic_skip_spaces(&p);
            if (*p == '=') {
                p++;
                basic_skip_spaces(&p);
                
                if (is_string) {
                    int idx = basic_get_var_index(var);
                    if (idx >= 0 && idx < BASIC_MAX_STRINGS) {
                        if (*p == '"') {
                            p++;
                            int i = 0;
                            while (*p && *p != '"' && i < BASIC_STRING_LEN - 1) {
                                strings[idx][i++] = *p++;
                            }
                            strings[idx][i] = '\0';
                        }
                    }
                } else {
                    int idx = basic_get_var_index(var);
                    if (idx >= 0) {
                        variables[idx] = basic_eval_expr(&p);
                    }
                }
            }
        }
        return;
    }
    
    /* GOTO */
    if (strncmp(p, "GOTO", 4) == 0) {
        p += 4;
        int target = (int)basic_eval_expr(&p);
        int idx = basic_find_line_ge(target);
        if (idx >= 0) {
            current_line_idx = idx;
        } else {
            vga_puts("?UNDEFINED LINE\n");
            running = false;
        }
        return;
    }
    
    /* GOSUB */
    if (strncmp(p, "GOSUB", 5) == 0) {
        p += 5;
        int target = (int)basic_eval_expr(&p);
        if (gosub_sp >= BASIC_STACK_SIZE) {
            vga_puts("?STACK OVERFLOW\n");
            running = false;
            return;
        }
        gosub_stack[gosub_sp++] = current_line_idx;
        int idx = basic_find_line_ge(target);
        if (idx >= 0) {
            current_line_idx = idx;
        } else {
            vga_puts("?UNDEFINED LINE\n");
            running = false;
        }
        return;
    }
    
    /* RETURN */
    if (strncmp(p, "RETURN", 6) == 0) {
        if (gosub_sp <= 0) {
            vga_puts("?RETURN WITHOUT GOSUB\n");
            running = false;
            return;
        }
        current_line_idx = gosub_stack[--gosub_sp] + 1;
        return;
    }
    
    /* IF/THEN */
    if (strncmp(p, "IF", 2) == 0) {
        p += 2;
        basic_skip_spaces(&p);
        
        int result = basic_eval_comparison(&p);
        
        basic_skip_spaces(&p);
        if (strncmp(p, "THEN", 4) == 0) {
            p += 4;
            basic_skip_spaces(&p);
        }
        
        if (result) {
            /* Check if THEN is followed by line number */
            if (*p >= '0' && *p <= '9') {
                int target = atoi(p);
                int idx = basic_find_line_ge(target);
                if (idx >= 0) {
                    current_line_idx = idx;
                }
            } else {
                /* Execute statement after THEN */
                basic_execute_statement(p);
            }
        }
        return;
    }
    
    /* FOR */
    if (strncmp(p, "FOR", 3) == 0) {
        p += 3;
        basic_skip_spaces(&p);
        
        if (for_sp >= BASIC_STACK_SIZE) {
            vga_puts("?STACK OVERFLOW\n");
            running = false;
            return;
        }
        
        char var = *p++;
        basic_skip_spaces(&p);
        if (*p == '=') p++;
        
        double start = basic_eval_expr(&p);
        
        basic_skip_spaces(&p);
        if (strncmp(p, "TO", 2) == 0) p += 2;
        
        double target = basic_eval_expr(&p);
        
        double step = 1;
        basic_skip_spaces(&p);
        if (strncmp(p, "STEP", 4) == 0) {
            p += 4;
            step = basic_eval_expr(&p);
        }
        
        int idx = basic_get_var_index(var);
        if (idx >= 0) {
            variables[idx] = start;
        }
        
        for_stack[for_sp].var = var;
        for_stack[for_sp].target = (int)target;
        for_stack[for_sp].step = (int)step;
        for_stack[for_sp].return_line = current_line_idx + 1;  /* Line AFTER FOR */
        for_sp++;
        return;
    }
    
    /* NEXT */
    if (strncmp(p, "NEXT", 4) == 0) {
        p += 4;
        basic_skip_spaces(&p);
        
        if (for_sp <= 0) {
            vga_puts("?NEXT WITHOUT FOR\n");
            running = false;
            return;
        }
        
        for_state_t* fs = &for_stack[for_sp - 1];
        int idx = basic_get_var_index(fs->var);
        if (idx >= 0) {
            variables[idx] += fs->step;
            
            bool done = (fs->step > 0) ? 
                        (variables[idx] > fs->target) : 
                        (variables[idx] < fs->target);
            
            if (!done) {
                /* Jump to line after FOR directly (main loop won't increment since we changed it) */
                current_line_idx = fs->return_line;
            } else {
                for_sp--;
                /* Don't change current_line_idx - let main loop advance past NEXT */
            }
        }
        return;
    }
    
    /* END */
    if (strncmp(p, "END", 3) == 0) {
        running = false;
        return;
    }
    
    /* CLS */
    if (strncmp(p, "CLS", 3) == 0) {
        vga_clear();
        return;
    }
    
    /* Implicit variable assignment (no LET keyword) - must be last */
    if ((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z')) {
        const char* start = p;
        char var = *p++;
        bool is_string = (*p == '$');
        if (is_string) p++;
        
        basic_skip_spaces(&p);
        if (*p == '=') {
            p++;
            basic_skip_spaces(&p);
            
            if (is_string) {
                int idx = basic_get_var_index(var);
                if (idx >= 0 && idx < BASIC_MAX_STRINGS) {
                    if (*p == '"') {
                        p++;
                        int i = 0;
                        while (*p && *p != '"' && i < BASIC_STRING_LEN - 1) {
                            strings[idx][i++] = *p++;
                        }
                        strings[idx][i] = '\0';
                    }
                }
            } else {
                int idx = basic_get_var_index(var);
                if (idx >= 0) {
                    variables[idx] = basic_eval_expr(&p);
                }
            }
            return;
        }
        /* Not an assignment, ignore */
        (void)start;
    }
}

/*
 * Execute a line (may contain multiple statements separated by :)
 */
static void basic_execute_line(const char* line) {
    char stmt[BASIC_MAX_LINE_LEN];
    const char* p = line;
    
    while (*p) {
        /* Extract statement up to : or end */
        int i = 0;
        bool in_string = false;
        while (*p && (in_string || *p != ':') && i < BASIC_MAX_LINE_LEN - 1) {
            if (*p == '"') in_string = !in_string;
            stmt[i++] = *p++;
        }
        stmt[i] = '\0';
        
        if (*p == ':') p++;
        
        basic_execute_statement(stmt);
        
        if (!running && current_line_idx >= 0) {
            /* GOTO/GOSUB changed line */
            return;
        }
    }
}

/*
 * RUN command
 */
static void basic_run_program(void) {
    if (num_lines == 0) {
        vga_puts("?NO PROGRAM\n");
        return;
    }
    
    running = true;
    current_line_idx = 0;
    gosub_sp = 0;
    for_sp = 0;
    
    while (running && current_line_idx < num_lines) {
        int saved_idx = current_line_idx;
        basic_execute_line(program[current_line_idx].text);
        
        /* Only advance if line wasn't changed by GOTO/GOSUB */
        if (current_line_idx == saved_idx) {
            current_line_idx++;
        }
    }
    
    running = false;
}

/*
 * Process immediate command or program line
 */
static void basic_process_input(const char* input) {
    const char* p = input;
    basic_skip_spaces(&p);
    
    if (*p == '\0') return;
    
    /* Check if line starts with number (program line) */
    if (*p >= '0' && *p <= '9') {
        int line_num = atoi(p);
        while (*p >= '0' && *p <= '9') p++;
        basic_skip_spaces(&p);
        
        if (*p == '\0') {
            /* Just a line number - delete line */
            basic_delete_line(line_num);
        } else {
            /* Insert/replace line */
            basic_insert_line(line_num, p);
        }
        return;
    }
    
    /* Immediate commands */
    if (strncmp(p, "RUN", 3) == 0 && (p[3] == '\0' || p[3] == ' ')) {
        basic_run_program();
        return;
    }
    
    if (strncmp(p, "LIST", 4) == 0) {
        p += 4;
        basic_skip_spaces(&p);
        int start = 0, end = BASIC_MAX_LINE_NUM;
        if (*p >= '0' && *p <= '9') {
            start = atoi(p);
            while (*p >= '0' && *p <= '9') p++;
            if (*p == '-') {
                p++;
                end = atoi(p);
            } else {
                end = start;
            }
        }
        basic_list(start, end);
        return;
    }
    
    if (strncmp(p, "NEW", 3) == 0) {
        basic_new();
        vga_puts("OK\n");
        return;
    }
    
    if (strncmp(p, "SAVE", 4) == 0) {
        p += 4;
        basic_skip_spaces(&p);
        if (*p == '"') p++;
        char fname[64];
        int i = 0;
        while (*p && *p != '"' && i < 63) fname[i++] = *p++;
        fname[i] = '\0';
        if (fname[0]) {
            basic_save(fname);
        } else if (filename[0]) {
            basic_save(filename);
        } else {
            vga_puts("?FILENAME REQUIRED\n");
        }
        return;
    }
    
    if (strncmp(p, "LOAD", 4) == 0) {
        p += 4;
        basic_skip_spaces(&p);
        if (*p == '"') p++;
        char fname[64];
        int i = 0;
        while (*p && *p != '"' && i < 63) fname[i++] = *p++;
        fname[i] = '\0';
        if (fname[0]) {
            basic_load(fname);
        } else {
            vga_puts("?FILENAME REQUIRED\n");
        }
        return;
    }
    
    if (strncmp(p, "BYE", 3) == 0 || strncmp(p, "QUIT", 4) == 0 || 
        strncmp(p, "EXIT", 4) == 0) {
        running = false;
        return;
    }
    
    if (strncmp(p, "HELP", 4) == 0) {
        vga_puts("MiniOS BASIC Commands:\n");
        vga_puts("  Program: RUN, LIST, NEW, SAVE, LOAD, BYE\n");
        vga_puts("  Statements: PRINT, INPUT, LET, IF/THEN\n");
        vga_puts("              GOTO, GOSUB, RETURN, FOR/NEXT\n");
        vga_puts("              END, REM, CLS\n");
        vga_puts("  Functions: RND, ABS, INT\n");
        vga_puts("  Variables: A-Z (numeric), A$-Z$ (string)\n");
        return;
    }
    
    /* Execute as immediate statement */
    running = true;
    current_line_idx = -1;
    basic_execute_statement(p);
    running = false;
}

/*
 * Main BASIC interpreter entry point
 */
void basic_run(const char* load_file) {
    vga_clear();
    vga_puts("MiniOS BASIC v1.0\n");
    vga_puts("Type HELP for commands, BYE to exit\n\n");
    
    basic_new();
    
    if (load_file && load_file[0]) {
        basic_load(load_file);
    }
    
    /* Main loop */
    bool quit = false;
    while (!quit) {
        vga_puts("READY\n");
        basic_getline(input_buffer, sizeof(input_buffer));
        
        /* Check for exit */
        const char* p = input_buffer;
        basic_skip_spaces(&p);
        if (strncmp(p, "BYE", 3) == 0 || strncmp(p, "QUIT", 4) == 0 ||
            strncmp(p, "EXIT", 4) == 0) {
            quit = true;
        } else {
            basic_process_input(input_buffer);
        }
    }
    
    vga_puts("\nReturned from BASIC.\n");
}
