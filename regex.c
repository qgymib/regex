#include <assert.h>
#include <wchar.h>
#include <wctype.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <stdint.h>
#include <limits.h>
#include <stdio.h>
#include "regex.h"

#define TRE_MEM_BLOCK_SIZE 1024
#define TRE_CHAR_MAX 0x10ffff
#undef CHARCLASS_NAME_MAX
#define CHARCLASS_NAME_MAX 14
#define MAX_NEG_CLASSES 64
#undef RE_DUP_MAX
#define RE_DUP_MAX 255

#define COPY_REMOVE_TAGS	 1
#define COPY_MAXIMIZE_FIRST_TAG	 2

#define ALIGN(ptr, type) \
  ((((ptrdiff_t)ptr) % sizeof(type)) \
   ? (sizeof(type) - (((ptrdiff_t)ptr) % sizeof(type))) \
   : 0)

#define xmalloc malloc
#define xcalloc calloc
#define xfree free
#define xrealloc realloc

#define tre_isalnum iswalnum
#define tre_isalpha iswalpha
#define tre_isblank iswblank
#define tre_iscntrl iswcntrl
#define tre_isdigit iswdigit
#define tre_isgraph iswgraph
#define tre_islower iswlower
#define tre_isprint iswprint
#define tre_ispunct iswpunct
#define tre_isspace iswspace
#define tre_isupper iswupper
#define tre_isxdigit iswxdigit

#define tre_tolower towlower
#define tre_toupper towupper
#define tre_strlen  wcslen

#define tre_mem_new()  tre_mem_new_impl(0, NULL)
#define tre_mem_new_impl   __tre_mem_new_impl
#define tre_mem_alloc_impl __tre_mem_alloc_impl
#define tre_mem_destroy    __tre_mem_destroy
#define tre_mem_calloc(mem, size) tre_mem_alloc_impl(mem, 0, NULL, 1, size)
#define tre_mem_alloc(mem, size) tre_mem_alloc_impl(mem, 0, NULL, 0, size)

#define tre_bt_mem_new		  tre_mem_new
#define tre_bt_mem_alloc	  tre_mem_alloc
#define tre_bt_mem_destroy	  tre_mem_destroy

#define PUSHPTR(err, s, v) do { \
	if ((err = tre_stack_push_voidptr(s, v)) != REG_OK) \
		return err; \
} while(0)

#define PUSHINT(err, s, v) do { \
	if ((err = tre_stack_push_int(s, v)) != REG_OK) \
		return err; \
} while(0)

#define EMPTY	  -1   /* Empty leaf (denotes empty string). */
#define ASSERTION -2   /* Assertion leaf. */
#define TAG	  -3   /* Tag leaf. */
#define BACKREF	  -4   /* Back reference leaf. */

#define IS_SPECIAL(x)	((x)->code_min < 0)
#define IS_EMPTY(x)	((x)->code_min == EMPTY)
#define IS_ASSERTION(x) ((x)->code_min == ASSERTION)
#define IS_TAG(x)	((x)->code_min == TAG)
#define IS_BACKREF(x)	((x)->code_min == BACKREF)

#define tre_isctype iswctype
#define tre_ctype   wctype

#define MAX(a, b) (((a) >= (b)) ? (a) : (b))
#define MIN(a, b) (((a) <= (b)) ? (a) : (b))

#define TRE_REGEX_T_FIELD __opaque

#define ASSERT_AT_BOL		  1   /* Beginning of line. */
#define ASSERT_AT_EOL		  2   /* End of line. */
#define ASSERT_CHAR_CLASS	  4   /* Character class in `class'. */
#define ASSERT_CHAR_CLASS_NEG	  8   /* Character classes in `neg_classes'. */
#define ASSERT_AT_BOW		 16   /* Beginning of word. */
#define ASSERT_AT_EOW		 32   /* End of word. */
#define ASSERT_AT_WB		 64   /* Word boundary. */
#define ASSERT_AT_WB_NEG	128   /* Not a word boundary. */
#define ASSERT_BACKREF		256   /* A back reference in `backref'. */
#define ASSERT_LAST		256

#define ERROR_EXIT(err)		  \
  do				  \
    {				  \
      errcode = err;		  \
      if (/*CONSTCOND*/1)	  \
      	goto error_exit;	  \
    }				  \
 while (/*CONSTCOND*/0)

#define STACK_PUSH(s, typetag, value)					      \
  do									      \
    {									      \
      status = tre_stack_push_ ## typetag(s, value);			      \
    }									      \
  while (/*CONSTCOND*/0)

#define STACK_PUSHX(s, typetag, value)					      \
  {									      \
    status = tre_stack_push_ ## typetag(s, value);			      \
    if (status != REG_OK)						      \
      break;								      \
  }

#define STACK_PUSHR(s, typetag, value)					      \
  {									      \
    reg_errcode_t _status;						      \
    _status = tre_stack_push_ ## typetag(s, value);			      \
    if (_status != REG_OK)						      \
      return _status;							      \
  }

#define IS_WORD_CHAR(c)	 ((c) == L'_' || tre_isalnum(c))

#define GET_NEXT_WCHAR() do {                                                 \
    prev_c = next_c; pos += pos_add_next;                                     \
    if ((pos_add_next = mbtowc(&next_c, str_byte, MB_LEN_MAX)) <= 0) {        \
        if (pos_add_next < 0) { ret = REG_NOMATCH; goto error_exit; }         \
        else pos_add_next++;                                                  \
    }                                                                         \
    str_byte += pos_add_next;                                                 \
  } while (0)

#define CHECK_ASSERTIONS(assertions)					      \
  (((assertions & ASSERT_AT_BOL)					      \
    && (pos > 0 || reg_notbol)						      \
    && (prev_c != L'\n' || !reg_newline))				      \
   || ((assertions & ASSERT_AT_EOL)					      \
       && (next_c != L'\0' || reg_noteol)				      \
       && (next_c != L'\n' || !reg_newline))				      \
   || ((assertions & ASSERT_AT_BOW)					      \
       && (IS_WORD_CHAR(prev_c) || !IS_WORD_CHAR(next_c)))	              \
   || ((assertions & ASSERT_AT_EOW)					      \
       && (!IS_WORD_CHAR(prev_c) || IS_WORD_CHAR(next_c)))		      \
   || ((assertions & ASSERT_AT_WB)					      \
       && (pos != 0 && next_c != L'\0'					      \
	   && IS_WORD_CHAR(prev_c) == IS_WORD_CHAR(next_c)))		      \
   || ((assertions & ASSERT_AT_WB_NEG)					      \
       && (pos == 0 || next_c == L'\0'					      \
	   || IS_WORD_CHAR(prev_c) != IS_WORD_CHAR(next_c))))

#define CHECK_CHAR_CLASSES(trans_i, tnfa, eflags)                             \
  (((trans_i->assertions & ASSERT_CHAR_CLASS)                                 \
       && !(tnfa->cflags & REG_ICASE)                                         \
       && !tre_isctype((tre_cint_t)prev_c, trans_i->u.class))                 \
    || ((trans_i->assertions & ASSERT_CHAR_CLASS)                             \
        && (tnfa->cflags & REG_ICASE)                                         \
        && !tre_isctype(tre_tolower((tre_cint_t)prev_c),trans_i->u.class)     \
	&& !tre_isctype(tre_toupper((tre_cint_t)prev_c),trans_i->u.class))    \
    || ((trans_i->assertions & ASSERT_CHAR_CLASS_NEG)                         \
        && tre_neg_char_classes_match(trans_i->neg_classes,(tre_cint_t)prev_c,\
                                      tnfa->cflags & REG_ICASE)))

#ifdef TRE_MBSTATE
#define BT_STACK_MBSTATE_IN  stack->item.mbstate = (mbstate)
#define BT_STACK_MBSTATE_OUT (mbstate) = stack->item.mbstate
#else /* !TRE_MBSTATE */
#define BT_STACK_MBSTATE_IN
#define BT_STACK_MBSTATE_OUT
#endif /* !TRE_MBSTATE */

#define BT_STACK_PUSH(_pos, _str_byte, _str_wide, _state, _state_id, _next_c, _tags, _mbstate) \
  do									      \
    {									      \
      int i;								      \
      if (!stack->next)							      \
	{								      \
	  tre_backtrack_t s;						      \
	  s = tre_bt_mem_alloc(mem, sizeof(*s));			      \
	  if (!s)							      \
	    {								      \
	      tre_bt_mem_destroy(mem);					      \
	      if (tags)							      \
		xfree(tags);						      \
	      if (pmatch)						      \
		xfree(pmatch);						      \
	      if (states_seen)						      \
		xfree(states_seen);					      \
	      return REG_ESPACE;					      \
	    }								      \
	  s->prev = stack;						      \
	  s->next = NULL;						      \
	  s->item.tags = tre_bt_mem_alloc(mem,				      \
					  sizeof(*tags) * tnfa->num_tags);    \
	  if (!s->item.tags)						      \
	    {								      \
	      tre_bt_mem_destroy(mem);					      \
	      if (tags)							      \
		xfree(tags);						      \
	      if (pmatch)						      \
		xfree(pmatch);						      \
	      if (states_seen)						      \
		xfree(states_seen);					      \
	      return REG_ESPACE;					      \
	    }								      \
	  stack->next = s;						      \
	  stack = s;							      \
	}								      \
      else								      \
	stack = stack->next;						      \
      stack->item.pos = (_pos);						      \
      stack->item.str_byte = (_str_byte);				      \
      stack->item.state = (_state);					      \
      stack->item.state_id = (_state_id);				      \
      stack->item.next_c = (_next_c);					      \
      for (i = 0; i < tnfa->num_tags; i++)				      \
	stack->item.tags[i] = (_tags)[i];				      \
      BT_STACK_MBSTATE_IN;						      \
    }									      \
  while (0)

#define BT_STACK_POP()							      \
  do									      \
    {									      \
      int i;								      \
      assert(stack->prev);						      \
      pos = stack->item.pos;						      \
      str_byte = stack->item.str_byte;					      \
      state = stack->item.state;					      \
      next_c = (tre_char_t)stack->item.next_c;					      \
      for (i = 0; i < tnfa->num_tags; i++)				      \
	tags[i] = stack->item.tags[i];					      \
      BT_STACK_MBSTATE_OUT;						      \
      stack = stack->prev;						      \
    }									      \
  while (0)

//typedef unsigned long wctype_t;
typedef wctype_t tre_ctype_t;
typedef wchar_t tre_char_t;
typedef struct tre_stack_rec tre_stack_t;

typedef enum {
  ADDTAGS_RECURSE,
  ADDTAGS_AFTER_ITERATION,
  ADDTAGS_AFTER_UNION_LEFT,
  ADDTAGS_AFTER_UNION_RIGHT,
  ADDTAGS_AFTER_CAT_LEFT,
  ADDTAGS_AFTER_CAT_RIGHT,
  ADDTAGS_SET_SUBMATCH_END
} tre_addtags_symbol_t;

typedef enum {
  COPY_RECURSE,
  COPY_SET_RESULT_PTR
} tre_copyast_symbol_t;

typedef enum {
  EXPAND_RECURSE,
  EXPAND_AFTER_ITER
} tre_expand_ast_symbol_t;

typedef enum {
  NFL_RECURSE,
  NFL_POST_UNION,
  NFL_POST_CATENATION,
  NFL_POST_ITERATION
} tre_nfl_stack_symbol_t;

union tre_stack_item {
  void *voidptr_value;
  int int_value;
};

struct tre_stack_rec {
  int size;
  int max_size;
  int increment;
  int ptr;
  union tre_stack_item *stack;
};

typedef enum {
  LITERAL,
  CATENATION,
  ITERATION,
  UNION
} tre_ast_type_t;

typedef struct {
  int position;
  int code_min;
  int code_max;
  int *tags;
  int assertions;
  tre_ctype_t class;
  tre_ctype_t *neg_classes;
  int backref;
} tre_pos_and_tags_t;

typedef struct {
  tre_ast_type_t type;   /* Type of the node. */
  void *obj;             /* Pointer to actual node. */
  int nullable;
  int submatch_id;
  int num_submatches;
  int num_tags;
  tre_pos_and_tags_t *firstpos;
  tre_pos_and_tags_t *lastpos;
} tre_ast_node_t;

typedef struct tnfa_transition tre_tnfa_transition_t;

//typedef unsigned wint_t;
typedef wint_t tre_cint_t;

struct tnfa_transition {
  /* Range of accepted characters. */
  tre_cint_t code_min;
  tre_cint_t code_max;
  /* Pointer to the destination state. */
  tre_tnfa_transition_t *state;
  /* ID number of the destination state. */
  int state_id;
  /* -1 terminated array of tags (or NULL). */
  int *tags;
  /* Assertion bitmap. */
  int assertions;
  /* Assertion parameters. */
  union {
    /* Character class assertion. */
    tre_ctype_t class;
    /* Back reference assertion. */
    int backref;
  } u;
  /* Negative character class assertions. */
  tre_ctype_t *neg_classes;
};

typedef struct tnfa tre_tnfa_t;

typedef struct tre_submatch_data tre_submatch_data_t;

struct tre_submatch_data {
  /* Tag that gives the value for rm_so (submatch start offset). */
  int so_tag;
  /* Tag that gives the value for rm_eo (submatch end offset). */
  int eo_tag;
  /* List of submatches this submatch is contained in. */
  int *parents;
};

typedef enum {
  TRE_TAG_MINIMIZE = 0,
  TRE_TAG_MAXIMIZE = 1
} tre_tag_direction_t;

struct tnfa {
  tre_tnfa_transition_t *transitions;
  unsigned int num_transitions;
  tre_tnfa_transition_t *initial;
  tre_tnfa_transition_t *final;
  tre_submatch_data_t *submatch_data;
  char *firstpos_chars;
  int first_char;
  unsigned int num_submatches;
  tre_tag_direction_t *tag_directions;
  int *minimal_tags;
  int num_tags;
  int num_minimals;
  int end_tag;
  int num_states;
  int cflags;
  int have_backrefs;
  int have_approx;
};

typedef int reg_errcode_t;

typedef struct tre_list {
  void *data;
  struct tre_list *next;
} tre_list_t;

typedef struct tre_mem_struct {
  tre_list_t *blocks;
  tre_list_t *current;
  char *ptr;
  size_t n;
  int failed;
  void **provided;
} *tre_mem_t;

typedef struct {
	/* Memory allocator. The AST is allocated using this. */
	tre_mem_t mem;
	/* Stack used for keeping track of regexp syntax. */
	tre_stack_t *stack;
	/* The parsed node after a parse function returns. */
	tre_ast_node_t *n;
	/* Position in the regexp pattern after a parse function returns. */
	const char *s;
	/* The first character of the last subexpression parsed. */
	const char *start;
	/* Current submatch ID. */
	int submatch_id;
	/* Current position (number of literal). */
	int position;
	/* The highest back reference or -1 if none seen so far. */
	int max_backref;
	/* Compilation flags. */
	int cflags;
} tre_parse_ctx_t;

typedef struct {
  long code_min;
  long code_max;
  int position;
  tre_ctype_t class;
  tre_ctype_t *neg_classes;
} tre_literal_t;

struct literals {
	tre_mem_t mem;
	tre_literal_t **a;
	int len;
	int cap;
};

struct neg {
	int negate;
	int len;
	tre_ctype_t a[MAX_NEG_CLASSES];
};

typedef struct {
  tre_ast_node_t *left;
  tre_ast_node_t *right;
} tre_union_t;

typedef struct {
  /* Subexpression to match. */
  tre_ast_node_t *arg;
  /* Minimum number of consecutive matches. */
  int min;
  /* Maximum number of consecutive matches. */
  int max;
  /* If 0, match as many characters as possible, if 1 match as few as
     possible.	Note that this does not always mean the same thing as
     matching as many/few repetitions as possible. */
  unsigned int minimal:1;
} tre_iteration_t;

typedef struct {
  tre_ast_node_t *left;
  tre_ast_node_t *right;
} tre_catenation_t;

typedef struct {
  int tag;
  int next_tag;
} tre_tag_states_t;

typedef struct {
  regoff_t pos;
  const char *str_byte;
  tre_tnfa_transition_t *state;
  int state_id;
  int next_c;
  regoff_t *tags;
#ifdef TRE_MBSTATE
  mbstate_t mbstate;
#endif /* TRE_MBSTATE */
} tre_backtrack_item_t;

typedef struct tre_backtrack_struct {
  tre_backtrack_item_t item;
  struct tre_backtrack_struct *prev;
  struct tre_backtrack_struct *next;
} *tre_backtrack_t;

typedef struct {
  tre_tnfa_transition_t *state;
  regoff_t *tags;
} tre_tnfa_reach_t;

typedef struct {
  regoff_t pos;
  regoff_t **tags;
} tre_reach_pos_t;

static const struct {
	char c;
	const char *expansion;
} tre_macros[] = {
	{'t', "\t"}, {'n', "\n"}, {'r', "\r"},
	{'f', "\f"}, {'a', "\a"}, {'e', "\033"},
	{'w', "[[:alnum:]_]"}, {'W', "[^[:alnum:]_]"}, {'s', "[[:space:]]"},
	{'S', "[^[:space:]]"}, {'d', "[[:digit:]]"}, {'D', "[^[:digit:]]"},
	{ 0, 0 }
};

static const char messages[] = {
  "No error\0"
  "No match\0"
  "Invalid regexp\0"
  "Unknown collating element\0"
  "Unknown character class name\0"
  "Trailing backslash\0"
  "Invalid back reference\0"
  "Missing ']'\0"
  "Missing ')'\0"
  "Missing '}'\0"
  "Invalid contents of {}\0"
  "Invalid character range\0"
  "Out of memory\0"
  "Repetition not preceded by valid expression\0"
  "\0Unknown error"
};

static reg_errcode_t tre_stack_push(tre_stack_t *s, union tre_stack_item value)
{
  if (s->ptr < s->size)
    {
      s->stack[s->ptr] = value;
      s->ptr++;
    }
  else
    {
      if (s->size >= s->max_size)
	{
	  return REG_ESPACE;
	}
      else
	{
	  union tre_stack_item *new_buffer;
	  int new_size;
	  new_size = s->size + s->increment;
	  if (new_size > s->max_size)
	    new_size = s->max_size;
	  new_buffer = xrealloc(s->stack, sizeof(*new_buffer) * new_size);
	  if (new_buffer == NULL)
	    {
	      return REG_ESPACE;
	    }
	  assert(new_size > s->size);
	  s->size = new_size;
	  s->stack = new_buffer;
	  tre_stack_push(s, value);
	}
    }
  return REG_OK;
}

#define declare_pushf(typetag, type)					      \
  static reg_errcode_t tre_stack_push_ ## typetag(tre_stack_t *s, type value)

#define define_pushf(typetag, type)  \
  declare_pushf(typetag, type) {     \
    union tre_stack_item item;	     \
    item.typetag ## _value = value;  \
    return tre_stack_push(s, item);  \
}

define_pushf(int, int)
define_pushf(voidptr, void *)

#define declare_popf(typetag, type)		  \
  static type tre_stack_pop_ ## typetag(tre_stack_t *s)

#define define_popf(typetag, type)		    \
  declare_popf(typetag, type) {			    \
    return s->stack[--s->ptr].typetag ## _value;    \
  }

define_popf(int, int)
define_popf(voidptr, void *)

static tre_mem_t tre_mem_new_impl(int provided, void *provided_block)
{
  tre_mem_t mem;
  if (provided)
    {
      mem = provided_block;
      memset(mem, 0, sizeof(*mem));
    }
  else
    mem = xcalloc(1, sizeof(*mem));
  if (mem == NULL)
    return NULL;
  return mem;
}

static tre_stack_t* tre_stack_new(int size, int max_size, int increment)
{
  tre_stack_t *s;

  s = xmalloc(sizeof(*s));
  if (s != NULL)
    {
      s->stack = xmalloc(sizeof(*s->stack) * size);
      if (s->stack == NULL)
	{
	  xfree(s);
	  return NULL;
	}
      s->size = size;
      s->max_size = max_size;
      s->increment = increment;
      s->ptr = 0;
    }
  return s;
}

static void tre_stack_destroy(tre_stack_t *s)
{
  xfree(s->stack);
  xfree(s);
}

static void * tre_mem_alloc_impl(tre_mem_t mem, int provided, void *provided_block,
		   int zero, size_t size)
{
  void *ptr;

  if (mem->failed)
    {
      return NULL;
    }

  if (mem->n < size)
    {
      /* We need more memory than is available in the current block.
	 Allocate a new block. */
      tre_list_t *l;
      if (provided)
	{
	  if (provided_block == NULL)
	    {
	      mem->failed = 1;
	      return NULL;
	    }
	  mem->ptr = provided_block;
	  mem->n = TRE_MEM_BLOCK_SIZE;
	}
      else
	{
	  size_t block_size;
	  if (size * 8 > TRE_MEM_BLOCK_SIZE)
	    block_size = size * 8;
	  else
	    block_size = TRE_MEM_BLOCK_SIZE;
	  l = xmalloc(sizeof(*l));
	  if (l == NULL)
	    {
	      mem->failed = 1;
	      return NULL;
	    }
	  l->data = xmalloc(block_size);
	  if (l->data == NULL)
	    {
	      xfree(l);
	      mem->failed = 1;
	      return NULL;
	    }
	  l->next = NULL;
	  if (mem->current != NULL)
	    mem->current->next = l;
	  if (mem->blocks == NULL)
	    mem->blocks = l;
	  mem->current = l;
	  mem->ptr = l->data;
	  mem->n = block_size;
	}
    }

  /* Make sure the next pointer will be aligned. */
  size += ALIGN(mem->ptr + size, long);

  /* Allocate from current block. */
  ptr = mem->ptr;
  mem->ptr += size;
  mem->n -= size;

  /* Set to zero if needed. */
  if (zero)
    memset(ptr, 0, size);

  return ptr;
}

static tre_ast_node_t * tre_ast_new_node(tre_mem_t mem, int type, void *obj)
{
	tre_ast_node_t *node = tre_mem_calloc(mem, sizeof *node);
	if (!node || !obj)
		return 0;
	node->obj = obj;
	node->type = type;
	node->nullable = -1;
	node->submatch_id = -1;
	return node;
}

static tre_ast_node_t * tre_ast_new_literal(tre_mem_t mem, int code_min, int code_max, int position)
{
	tre_ast_node_t *node;
	tre_literal_t *lit;

	lit = tre_mem_calloc(mem, sizeof *lit);
	node = tre_ast_new_node(mem, LITERAL, lit);
	if (!node)
		return 0;
	lit->code_min = code_min;
	lit->code_max = code_max;
	lit->position = position;
	return node;
}

static tre_literal_t *tre_new_lit(struct literals *p)
{
	tre_literal_t **a;
	if (p->len >= p->cap) {
		if (p->cap >= 1<<15)
			return 0;
		p->cap *= 2;
		a = xrealloc(p->a, p->cap * sizeof *p->a);
		if (!a)
			return 0;
		p->a = a;
	}
	a = p->a + p->len++;
	*a = tre_mem_calloc(p->mem, sizeof **a);
	return *a;
}

static int add_icase_literals(struct literals *ls, int min, int max)
{
	tre_literal_t *lit;
	int b, e, c;
	for (c=min; c<=max; ) {
		/* assumes islower(c) and isupper(c) are exclusive
		   and toupper(c)!=c if islower(c).
		   multiple opposite case characters are not supported */
		if (tre_islower((wint_t)c)) {
			b = e = tre_toupper((wint_t)c);
			for (c++, e++; c<=max; c++, e++)
				if ((int)tre_toupper((wint_t)c) != e) break;
		} else if (tre_isupper((wint_t)c)) {
			b = e = tre_tolower((wint_t)c);
			for (c++, e++; c<=max; c++, e++)
				if ((int)tre_tolower((wint_t)c) != e) break;
		} else {
			c++;
			continue;
		}
		lit = tre_new_lit(ls);
		if (!lit)
			return -1;
		lit->code_min = b;
		lit->code_max = e-1;
		lit->position = -1;
	}
	return 0;
}

static reg_errcode_t parse_bracket_terms(tre_parse_ctx_t *ctx, const char *s, struct literals *ls, struct neg *neg)
{
	const char *start = s;
	tre_ctype_t class;
	int min, max;
	wchar_t wc;
	int len;

	for (;;) {
		class = 0;
		len = mbtowc(&wc, s, (size_t)-1);
		if (len <= 0)
			return *s ? REG_BADPAT : REG_EBRACK;
		if (*s == ']' && s != start) {
			ctx->s = s+1;
			return REG_OK;
		}
		if (*s == '-' && s != start && s[1] != ']' &&
		    /* extension: [a-z--@] is accepted as [a-z]|[--@] */
		    (s[1] != '-' || s[2] == ']'))
			return REG_ERANGE;
		if (*s == '[' && (s[1] == '.' || s[1] == '='))
			/* collating symbols and equivalence classes are not supported */
			return REG_ECOLLATE;
		if (*s == '[' && s[1] == ':') {
			char tmp[CHARCLASS_NAME_MAX+1];
			s += 2;
			for (len=0; len < CHARCLASS_NAME_MAX && s[len]; len++) {
				if (s[len] == ':') {
					memcpy(tmp, s, len);
					tmp[len] = 0;
					class = tre_ctype(tmp);
					break;
				}
			}
			if (!class || s[len+1] != ']')
				return REG_ECTYPE;
			min = 0;
			max = TRE_CHAR_MAX;
			s += len+2;
		} else {
			min = max = wc;
			s += len;
			if (*s == '-' && s[1] != ']') {
				s++;
				len = mbtowc(&wc, s, (size_t)-1);
				max = wc;
				/* XXX - Should use collation order instead of
				   encoding values in character ranges. */
				if (len <= 0 || min > max)
					return REG_ERANGE;
				s += len;
			}
		}

		if (class && neg->negate) {
			if (neg->len >= MAX_NEG_CLASSES)
				return REG_ESPACE;
			neg->a[neg->len++] = class;
		} else  {
			tre_literal_t *lit = tre_new_lit(ls);
			if (!lit)
				return REG_ESPACE;
			lit->code_min = min;
			lit->code_max = max;
			lit->class = class;
			lit->position = -1;

			/* Add opposite-case codepoints if REG_ICASE is present.
			   It seems that POSIX requires that bracket negation
			   should happen before case-folding, but most practical
			   implementations do it the other way around. Changing
			   the order would need efficient representation of
			   case-fold ranges and bracket range sets even with
			   simple patterns so this is ok for now. */
			if (ctx->cflags & REG_ICASE && !class)
				if (add_icase_literals(ls, min, max))
					return REG_ESPACE;
		}
	}
}

static int tre_compare_lit(const void *a, const void *b)
{
	const tre_literal_t *const *la = a;
	const tre_literal_t *const *lb = b;
	/* assumes the range of valid code_min is < INT_MAX */
	return la[0]->code_min - lb[0]->code_min;
}

static tre_ast_node_t * tre_ast_new_union(tre_mem_t mem, tre_ast_node_t *left, tre_ast_node_t *right)
{
	tre_ast_node_t *node;
	tre_union_t *un;

	if (!left)
		return right;
	un = tre_mem_calloc(mem, sizeof *un);
	node = tre_ast_new_node(mem, UNION, un);
	if (!node || !right)
		return 0;
	un->left = left;
	un->right = right;
	node->num_submatches = left->num_submatches + right->num_submatches;
	return node;
}

static reg_errcode_t parse_bracket(tre_parse_ctx_t *ctx, const char *s)
{
	int i, max, min, negmax, negmin;
	tre_ast_node_t *node = 0, *n;
	tre_ctype_t *nc = 0;
	tre_literal_t *lit;
	struct literals ls;
	struct neg neg;
	reg_errcode_t err;

	ls.mem = ctx->mem;
	ls.len = 0;
	ls.cap = 32;
	ls.a = xmalloc(ls.cap * sizeof *ls.a);
	if (!ls.a)
		return REG_ESPACE;
	neg.len = 0;
	neg.negate = *s == '^';
	if (neg.negate)
		s++;

	err = parse_bracket_terms(ctx, s, &ls, &neg);
	if (err != REG_OK)
		goto parse_bracket_done;

	if (neg.negate) {
		/*
		 * With REG_NEWLINE, POSIX requires that newlines are not matched by
		 * any form of a non-matching list.
		 */
		if (ctx->cflags & REG_NEWLINE) {
			lit = tre_new_lit(&ls);
			if (!lit) {
				err = REG_ESPACE;
				goto parse_bracket_done;
			}
			lit->code_min = '\n';
			lit->code_max = '\n';
			lit->position = -1;
		}
		/* Sort the array if we need to negate it. */
		qsort(ls.a, ls.len, sizeof *ls.a, tre_compare_lit);
		/* extra lit for the last negated range */
		lit = tre_new_lit(&ls);
		if (!lit) {
			err = REG_ESPACE;
			goto parse_bracket_done;
		}
		lit->code_min = TRE_CHAR_MAX+1;
		lit->code_max = TRE_CHAR_MAX+1;
		lit->position = -1;
		/* negated classes */
		if (neg.len) {
			nc = tre_mem_alloc(ctx->mem, (neg.len+1)*sizeof *neg.a);
			if (!nc) {
				err = REG_ESPACE;
				goto parse_bracket_done;
			}
			memcpy(nc, neg.a, neg.len*sizeof *neg.a);
			nc[neg.len] = 0;
		}
	}

	/* Build a union of the items in the array, negated if necessary. */
	negmax = negmin = 0;
	for (i = 0; i < ls.len; i++) {
		lit = ls.a[i];
		min = lit->code_min;
		max = lit->code_max;
		if (neg.negate) {
			if (min <= negmin) {
				/* Overlap. */
				negmin = MAX(max + 1, negmin);
				continue;
			}
			negmax = min - 1;
			lit->code_min = negmin;
			lit->code_max = negmax;
			negmin = max + 1;
		}
		lit->position = ctx->position;
		lit->neg_classes = nc;
		n = tre_ast_new_node(ctx->mem, LITERAL, lit);
		node = tre_ast_new_union(ctx->mem, node, n);
		if (!node) {
			err = REG_ESPACE;
			break;
		}
	}

parse_bracket_done:
	xfree(ls.a);
	ctx->position++;
	ctx->n = node;
	return err;
}

static const char *tre_expand_macro(const char *s)
{
	int i;
	for (i = 0; tre_macros[i].c && tre_macros[i].c != *s; i++);
	return tre_macros[i].expansion;
}

static int hexval(unsigned c)
{
	if (c-'0'<10) return c-'0';
	c |= 32;
	if (c-'a'<6) return c-'a'+10;
	return -1;
}

static reg_errcode_t parse_atom(tre_parse_ctx_t *ctx, const char *s)
{
	int len, ere = ctx->cflags & REG_EXTENDED;
	const char *p;
	tre_ast_node_t *node;
	wchar_t wc;
	switch (*s) {
	case '[':
		return parse_bracket(ctx, s+1);
	case '\\':
		p = tre_expand_macro(s+1);
		if (p) {
			/* assume \X expansion is a single atom */
			reg_errcode_t err = parse_atom(ctx, p);
			ctx->s = s+2;
			return err;
		}
		/* extensions: \b, \B, \<, \>, \xHH \x{HHHH} */
		switch (*++s) {
		case 0:
			return REG_EESCAPE;
		case 'b':
			node = tre_ast_new_literal(ctx->mem, ASSERTION, ASSERT_AT_WB, -1);
			break;
		case 'B':
			node = tre_ast_new_literal(ctx->mem, ASSERTION, ASSERT_AT_WB_NEG, -1);
			break;
		case '<':
			node = tre_ast_new_literal(ctx->mem, ASSERTION, ASSERT_AT_BOW, -1);
			break;
		case '>':
			node = tre_ast_new_literal(ctx->mem, ASSERTION, ASSERT_AT_EOW, -1);
			break;
		case 'x':
			s++;
			int i, v = 0, c;
			len = 2;
			if (*s == '{') {
				len = 8;
				s++;
			}
			for (i=0; i<len && v<0x110000; i++) {
				c = hexval(s[i]);
				if (c < 0) break;
				v = 16*v + c;
			}
			s += i;
			if (len == 8) {
				if (*s != '}')
					return REG_EBRACE;
				s++;
			}
			node = tre_ast_new_literal(ctx->mem, v, v, ctx->position++);
			s--;
			break;
		case '{':
		case '+':
		case '?':
			/* extension: treat \+, \? as repetitions in BRE */
			/* reject repetitions after empty expression in BRE */
			if (!ere)
				return REG_BADRPT;
			/* FALLTHROUGH */
		case '|':
			/* extension: treat \| as alternation in BRE */
			if (!ere) {
				node = tre_ast_new_literal(ctx->mem, EMPTY, -1, -1);
				s--;
				goto end;
			}
			/* fallthrough */
		default:
			if (!ere && (unsigned)*s-'1' < 9) {
				/* back reference */
				int val = *s - '0';
				node = tre_ast_new_literal(ctx->mem, BACKREF, val, ctx->position++);
				ctx->max_backref = MAX(val, ctx->max_backref);
			} else {
				/* extension: accept unknown escaped char
				   as a literal */
				goto parse_literal;
			}
		}
		s++;
		break;
	case '.':
		if (ctx->cflags & REG_NEWLINE) {
			tre_ast_node_t *tmp1, *tmp2;
			tmp1 = tre_ast_new_literal(ctx->mem, 0, '\n'-1, ctx->position++);
			tmp2 = tre_ast_new_literal(ctx->mem, '\n'+1, TRE_CHAR_MAX, ctx->position++);
			if (tmp1 && tmp2)
				node = tre_ast_new_union(ctx->mem, tmp1, tmp2);
			else
				node = 0;
		} else {
			node = tre_ast_new_literal(ctx->mem, 0, TRE_CHAR_MAX, ctx->position++);
		}
		s++;
		break;
	case '^':
		/* '^' has a special meaning everywhere in EREs, and at beginning of BRE. */
		if (!ere && s != ctx->start)
			goto parse_literal;
		node = tre_ast_new_literal(ctx->mem, ASSERTION, ASSERT_AT_BOL, -1);
		s++;
		break;
	case '$':
		/* '$' is special everywhere in EREs, and at the end of a BRE subexpression. */
		if (!ere && s[1] && (s[1]!='\\'|| (s[2]!=')' && s[2]!='|')))
			goto parse_literal;
		node = tre_ast_new_literal(ctx->mem, ASSERTION, ASSERT_AT_EOL, -1);
		s++;
		break;
	case '*':
	case '{':
	case '+':
	case '?':
		/* reject repetitions after empty expression in ERE */
		if (ere)
			return REG_BADRPT;
		/* FALLTHROUGH */
	case '|':
		if (!ere)
			goto parse_literal;
		/* FALLTHROUGH */
	case 0:
		node = tre_ast_new_literal(ctx->mem, EMPTY, -1, -1);
		break;
	default:
parse_literal:
		len = mbtowc(&wc, s, (size_t)-1);
		if (len < 0)
			return REG_BADPAT;
		if (ctx->cflags & REG_ICASE && (tre_isupper(wc) || tre_islower(wc))) {
			tre_ast_node_t *tmp1, *tmp2;
			/* multiple opposite case characters are not supported */
			tmp1 = tre_ast_new_literal(ctx->mem, tre_toupper(wc), tre_toupper(wc), ctx->position);
			tmp2 = tre_ast_new_literal(ctx->mem, tre_tolower(wc), tre_tolower(wc), ctx->position);
			if (tmp1 && tmp2)
				node = tre_ast_new_union(ctx->mem, tmp1, tmp2);
			else
				node = 0;
		} else {
			node = tre_ast_new_literal(ctx->mem, wc, wc, ctx->position);
		}
		ctx->position++;
		s += len;
		break;
	}
end:
	if (!node)
		return REG_ESPACE;
	ctx->n = node;
	ctx->s = s;
	return REG_OK;
}

static const char *parse_dup_count(const char *s, int *n)
{
	*n = -1;
	if (!isdigit(*s))
		return s;
	*n = 0;
	for (;;) {
		*n = 10 * *n + (*s - '0');
		s++;
		if (!isdigit(*s) || *n > RE_DUP_MAX)
			break;
	}
	return s;
}

static const char *parse_dup(const char *s, int ere, int *pmin, int *pmax)
{
	int min, max;

	s = parse_dup_count(s, &min);
	if (*s == ',')
		s = parse_dup_count(s+1, &max);
	else
		max = min;

	if (
		(max < min && max >= 0) ||
		max > RE_DUP_MAX ||
		min > RE_DUP_MAX ||
		min < 0 ||
		(!ere && *s++ != '\\') ||
		*s++ != '}'
	)
		return 0;
	*pmin = min;
	*pmax = max;
	return s;
}

static tre_ast_node_t * tre_ast_new_iter(tre_mem_t mem, tre_ast_node_t *arg, int min, int max, int minimal)
{
	tre_ast_node_t *node;
	tre_iteration_t *iter;

	iter = tre_mem_calloc(mem, sizeof *iter);
	node = tre_ast_new_node(mem, ITERATION, iter);
	if (!node)
		return 0;
	iter->arg = arg;
	iter->min = min;
	iter->max = max;
	iter->minimal = minimal;
	node->num_submatches = arg->num_submatches;
	return node;
}

static tre_ast_node_t * tre_ast_new_catenation(tre_mem_t mem, tre_ast_node_t *left, tre_ast_node_t *right)
{
	tre_ast_node_t *node;
	tre_catenation_t *cat;

	if (!left)
		return right;
	cat = tre_mem_calloc(mem, sizeof *cat);
	node = tre_ast_new_node(mem, CATENATION, cat);
	if (!node)
		return 0;
	cat->left = left;
	cat->right = right;
	node->num_submatches = left->num_submatches + right->num_submatches;
	return node;
}

static reg_errcode_t marksub(tre_parse_ctx_t *ctx, tre_ast_node_t *node, int subid)
{
	if (node->submatch_id >= 0) {
		tre_ast_node_t *n = tre_ast_new_literal(ctx->mem, EMPTY, -1, -1);
		if (!n)
			return REG_ESPACE;
		n = tre_ast_new_catenation(ctx->mem, n, node);
		if (!n)
			return REG_ESPACE;
		n->num_submatches = node->num_submatches;
		node = n;
	}
	node->submatch_id = subid;
	node->num_submatches++;
	ctx->n = node;
	return REG_OK;
}

static reg_errcode_t tre_parse(tre_parse_ctx_t *ctx)
{
	tre_ast_node_t *nbranch=0, *nunion=0;
	int ere = ctx->cflags & REG_EXTENDED;
	const char *s = ctx->start;
	int subid = 0;
	int depth = 0;
	reg_errcode_t err;
	tre_stack_t *stack = ctx->stack;

	PUSHINT(err, stack, subid++);
	for (;;) {
		if ((!ere && *s == '\\' && s[1] == '(') ||
		    (ere && *s == '(')) {
			PUSHPTR(err, stack, nunion);
			PUSHPTR(err, stack, nbranch);
			PUSHINT(err, stack, subid++);
			s++;
			if (!ere)
				s++;
			depth++;
			nbranch = nunion = 0;
			ctx->start = s;
			continue;
		}
		if ((!ere && *s == '\\' && s[1] == ')') ||
		    (ere && *s == ')' && depth)) {
			ctx->n = tre_ast_new_literal(ctx->mem, EMPTY, -1, -1);
			if (!ctx->n)
				return REG_ESPACE;
		} else {
			err = parse_atom(ctx, s);
			if (err != REG_OK)
				return err;
			s = ctx->s;
		}

	parse_iter:
		for (;;) {
			int min, max;

			if (*s!='\\' && *s!='*') {
				if (!ere)
					break;
				if (*s!='+' && *s!='?' && *s!='{')
					break;
			}
			if (*s=='\\' && ere)
				break;
			/* extension: treat \+, \? as repetitions in BRE */
			if (*s=='\\' && s[1]!='+' && s[1]!='?' && s[1]!='{')
				break;
			if (*s=='\\')
				s++;

			/* handle ^* at the start of a BRE. */
			if (!ere && s==ctx->start+1 && s[-1]=='^')
				break;

			/* extension: multiple consecutive *+?{,} is unspecified,
			   but (a+)+ has to be supported so accepting a++ makes
			   sense, note however that the RE_DUP_MAX limit can be
			   circumvented: (a{255}){255} uses a lot of memory.. */
			if (*s=='{') {
				s = parse_dup(s+1, ere, &min, &max);
				if (!s)
					return REG_BADBR;
			} else {
				min=0;
				max=-1;
				if (*s == '+')
					min = 1;
				if (*s == '?')
					max = 1;
				s++;
			}
			if (max == 0)
				ctx->n = tre_ast_new_literal(ctx->mem, EMPTY, -1, -1);
			else
				ctx->n = tre_ast_new_iter(ctx->mem, ctx->n, min, max, 0);
			if (!ctx->n)
				return REG_ESPACE;
		}

		nbranch = tre_ast_new_catenation(ctx->mem, nbranch, ctx->n);
		if ((ere && *s == '|') ||
		    (ere && *s == ')' && depth) ||
		    (!ere && *s == '\\' && s[1] == ')') ||
		    /* extension: treat \| as alternation in BRE */
		    (!ere && *s == '\\' && s[1] == '|') ||
		    !*s) {
			/* extension: empty branch is unspecified (), (|a), (a|)
			   here they are not rejected but match on empty string */
			int c = *s;
			nunion = tre_ast_new_union(ctx->mem, nunion, nbranch);
			nbranch = 0;

			if (c == '\\' && s[1] == '|') {
				s+=2;
				ctx->start = s;
			} else if (c == '|') {
				s++;
				ctx->start = s;
			} else {
				if (c == '\\') {
					if (!depth) return REG_EPAREN;
					s+=2;
				} else if (c == ')')
					s++;
				depth--;
				err = marksub(ctx, nunion, tre_stack_pop_int(stack));
				if (err != REG_OK)
					return err;
				if (!c && depth<0) {
					ctx->submatch_id = subid;
					return REG_OK;
				}
				if (!c || depth<0)
					return REG_EPAREN;
				nbranch = tre_stack_pop_voidptr(stack);
				nunion = tre_stack_pop_voidptr(stack);
				goto parse_iter;
			}
		}
	}
}

static int tre_stack_num_objects(tre_stack_t *s)
{
  return s->ptr;
}

static reg_errcode_t tre_add_tag_left(tre_mem_t mem, tre_ast_node_t *node, int tag_id)
{
  tre_catenation_t *c;

  c = tre_mem_alloc(mem, sizeof(*c));
  if (c == NULL)
    return REG_ESPACE;
  c->left = tre_ast_new_literal(mem, TAG, tag_id, -1);
  if (c->left == NULL)
    return REG_ESPACE;
  c->right = tre_mem_alloc(mem, sizeof(tre_ast_node_t));
  if (c->right == NULL)
    return REG_ESPACE;

  c->right->obj = node->obj;
  c->right->type = node->type;
  c->right->nullable = -1;
  c->right->submatch_id = -1;
  c->right->firstpos = NULL;
  c->right->lastpos = NULL;
  c->right->num_tags = 0;
  c->right->num_submatches = 0;
  node->obj = c;
  node->type = CATENATION;
  return REG_OK;
}

static void tre_purge_regset(int *regset, tre_tnfa_t *tnfa, int tag)
{
  int i;

  for (i = 0; regset[i] >= 0; i++)
    {
      int id = regset[i] / 2;
      int start = !(regset[i] % 2);
      if (start)
	tnfa->submatch_data[id].so_tag = tag;
      else
	tnfa->submatch_data[id].eo_tag = tag;
    }
  regset[0] = -1;
}

static reg_errcode_t tre_add_tag_right(tre_mem_t mem, tre_ast_node_t *node, int tag_id)
{
  tre_catenation_t *c;

  c = tre_mem_alloc(mem, sizeof(*c));
  if (c == NULL)
    return REG_ESPACE;
  c->right = tre_ast_new_literal(mem, TAG, tag_id, -1);
  if (c->right == NULL)
    return REG_ESPACE;
  c->left = tre_mem_alloc(mem, sizeof(tre_ast_node_t));
  if (c->left == NULL)
    return REG_ESPACE;

  c->left->obj = node->obj;
  c->left->type = node->type;
  c->left->nullable = -1;
  c->left->submatch_id = -1;
  c->left->firstpos = NULL;
  c->left->lastpos = NULL;
  c->left->num_tags = 0;
  c->left->num_submatches = 0;
  node->obj = c;
  node->type = CATENATION;
  return REG_OK;
}

static reg_errcode_t tre_add_tags(tre_mem_t mem, tre_stack_t *stack, tre_ast_node_t *tree,
	     tre_tnfa_t *tnfa)
{
  reg_errcode_t status = REG_OK;
  tre_addtags_symbol_t symbol;
  tre_ast_node_t *node = tree; /* Tree node we are currently looking at. */
  int bottom = tre_stack_num_objects(stack);
  /* True for first pass (counting number of needed tags) */
  int first_pass = (mem == NULL || tnfa == NULL);
  int *regset, *orig_regset;
  int num_tags = 0; /* Total number of tags. */
  int num_minimals = 0;	 /* Number of special minimal tags. */
  int tag = 0;	    /* The tag that is to be added next. */
  int next_tag = 1; /* Next tag to use after this one. */
  int *parents;	    /* Stack of submatches the current submatch is
		       contained in. */
  int minimal_tag = -1; /* Tag that marks the beginning of a minimal match. */
  tre_tag_states_t *saved_states;

  tre_tag_direction_t direction = TRE_TAG_MINIMIZE;
  if (!first_pass)
    {
      tnfa->end_tag = 0;
      tnfa->minimal_tags[0] = -1;
    }

  regset = xmalloc(sizeof(*regset) * ((tnfa->num_submatches + 1) * 2));
  if (regset == NULL)
    return REG_ESPACE;
  regset[0] = -1;
  orig_regset = regset;

  parents = xmalloc(sizeof(*parents) * (tnfa->num_submatches + 1));
  if (parents == NULL)
    {
      xfree(regset);
      return REG_ESPACE;
    }
  parents[0] = -1;

  saved_states = xmalloc(sizeof(*saved_states) * (tnfa->num_submatches + 1));
  if (saved_states == NULL)
    {
      xfree(regset);
      xfree(parents);
      return REG_ESPACE;
    }
  else
    {
      unsigned int i;
      for (i = 0; i <= tnfa->num_submatches; i++)
	saved_states[i].tag = -1;
    }

  STACK_PUSH(stack, voidptr, node);
  STACK_PUSH(stack, int, ADDTAGS_RECURSE);

  while (tre_stack_num_objects(stack) > bottom)
    {
      if (status != REG_OK)
	break;

      symbol = (tre_addtags_symbol_t)tre_stack_pop_int(stack);
      switch (symbol)
	{

	case ADDTAGS_SET_SUBMATCH_END:
	  {
	    int id = tre_stack_pop_int(stack);
	    int i;

	    /* Add end of this submatch to regset. */
	    for (i = 0; regset[i] >= 0; i++);
	    regset[i] = id * 2 + 1;
	    regset[i + 1] = -1;

	    /* Pop this submatch from the parents stack. */
	    for (i = 0; parents[i] >= 0; i++);
	    parents[i - 1] = -1;
	    break;
	  }

	case ADDTAGS_RECURSE:
	  node = tre_stack_pop_voidptr(stack);

	  if (node->submatch_id >= 0)
	    {
	      int id = node->submatch_id;
	      int i;


	      /* Add start of this submatch to regset. */
	      for (i = 0; regset[i] >= 0; i++);
	      regset[i] = id * 2;
	      regset[i + 1] = -1;

	      if (!first_pass)
		{
		  for (i = 0; parents[i] >= 0; i++);
		  tnfa->submatch_data[id].parents = NULL;
		  if (i > 0)
		    {
		      int *p = xmalloc(sizeof(*p) * (i + 1));
		      if (p == NULL)
			{
			  status = REG_ESPACE;
			  break;
			}
		      assert(tnfa->submatch_data[id].parents == NULL);
		      tnfa->submatch_data[id].parents = p;
		      for (i = 0; parents[i] >= 0; i++)
			p[i] = parents[i];
		      p[i] = -1;
		    }
		}

	      /* Add end of this submatch to regset after processing this
		 node. */
	      STACK_PUSHX(stack, int, node->submatch_id);
	      STACK_PUSHX(stack, int, ADDTAGS_SET_SUBMATCH_END);
	    }

	  switch (node->type)
	    {
	    case LITERAL:
	      {
		tre_literal_t *lit = node->obj;

		if (!IS_SPECIAL(lit) || IS_BACKREF(lit))
		  {
		    int i;
		    if (regset[0] >= 0)
		      {
			/* Regset is not empty, so add a tag before the
			   literal or backref. */
			if (!first_pass)
			  {
			    status = tre_add_tag_left(mem, node, tag);
			    tnfa->tag_directions[tag] = direction;
			    if (minimal_tag >= 0)
			      {
				for (i = 0; tnfa->minimal_tags[i] >= 0; i++);
				tnfa->minimal_tags[i] = tag;
				tnfa->minimal_tags[i + 1] = minimal_tag;
				tnfa->minimal_tags[i + 2] = -1;
				minimal_tag = -1;
				num_minimals++;
			      }
			    tre_purge_regset(regset, tnfa, tag);
			  }
			else
			  {
			    node->num_tags = 1;
			  }

			regset[0] = -1;
			tag = next_tag;
			num_tags++;
			next_tag++;
		      }
		  }
		else
		  {
		    assert(!IS_TAG(lit));
		  }
		break;
	      }
	    case CATENATION:
	      {
		tre_catenation_t *cat = node->obj;
		tre_ast_node_t *left = cat->left;
		tre_ast_node_t *right = cat->right;
		int reserved_tag = -1;


		/* After processing right child. */
		STACK_PUSHX(stack, voidptr, node);
		STACK_PUSHX(stack, int, ADDTAGS_AFTER_CAT_RIGHT);

		/* Process right child. */
		STACK_PUSHX(stack, voidptr, right);
		STACK_PUSHX(stack, int, ADDTAGS_RECURSE);

		/* After processing left child. */
		STACK_PUSHX(stack, int, next_tag + left->num_tags);
		if (left->num_tags > 0 && right->num_tags > 0)
		  {
		    /* Reserve the next tag to the right child. */
		    reserved_tag = next_tag;
		    next_tag++;
		  }
		STACK_PUSHX(stack, int, reserved_tag);
		STACK_PUSHX(stack, int, ADDTAGS_AFTER_CAT_LEFT);

		/* Process left child. */
		STACK_PUSHX(stack, voidptr, left);
		STACK_PUSHX(stack, int, ADDTAGS_RECURSE);

		}
	      break;
	    case ITERATION:
	      {
		tre_iteration_t *iter = node->obj;

		if (first_pass)
		  {
		    STACK_PUSHX(stack, int, regset[0] >= 0 || iter->minimal);
		  }
		else
		  {
		    STACK_PUSHX(stack, int, tag);
		    STACK_PUSHX(stack, int, iter->minimal);
		  }
		STACK_PUSHX(stack, voidptr, node);
		STACK_PUSHX(stack, int, ADDTAGS_AFTER_ITERATION);

		STACK_PUSHX(stack, voidptr, iter->arg);
		STACK_PUSHX(stack, int, ADDTAGS_RECURSE);

		/* Regset is not empty, so add a tag here. */
		if (regset[0] >= 0 || iter->minimal)
		  {
		    if (!first_pass)
		      {
			int i;
			status = tre_add_tag_left(mem, node, tag);
			if (iter->minimal)
			  tnfa->tag_directions[tag] = TRE_TAG_MAXIMIZE;
			else
			  tnfa->tag_directions[tag] = direction;
			if (minimal_tag >= 0)
			  {
			    for (i = 0; tnfa->minimal_tags[i] >= 0; i++);
			    tnfa->minimal_tags[i] = tag;
			    tnfa->minimal_tags[i + 1] = minimal_tag;
			    tnfa->minimal_tags[i + 2] = -1;
			    minimal_tag = -1;
			    num_minimals++;
			  }
			tre_purge_regset(regset, tnfa, tag);
		      }

		    regset[0] = -1;
		    tag = next_tag;
		    num_tags++;
		    next_tag++;
		  }
		direction = TRE_TAG_MINIMIZE;
	      }
	      break;
	    case UNION:
	      {
		tre_union_t *uni = node->obj;
		tre_ast_node_t *left = uni->left;
		tre_ast_node_t *right = uni->right;
		int left_tag;
		int right_tag;

		if (regset[0] >= 0)
		  {
		    left_tag = next_tag;
		    right_tag = next_tag + 1;
		  }
		else
		  {
		    left_tag = tag;
		    right_tag = next_tag;
		  }

		/* After processing right child. */
		STACK_PUSHX(stack, int, right_tag);
		STACK_PUSHX(stack, int, left_tag);
		STACK_PUSHX(stack, voidptr, regset);
		STACK_PUSHX(stack, int, regset[0] >= 0);
		STACK_PUSHX(stack, voidptr, node);
		STACK_PUSHX(stack, voidptr, right);
		STACK_PUSHX(stack, voidptr, left);
		STACK_PUSHX(stack, int, ADDTAGS_AFTER_UNION_RIGHT);

		/* Process right child. */
		STACK_PUSHX(stack, voidptr, right);
		STACK_PUSHX(stack, int, ADDTAGS_RECURSE);

		/* After processing left child. */
		STACK_PUSHX(stack, int, ADDTAGS_AFTER_UNION_LEFT);

		/* Process left child. */
		STACK_PUSHX(stack, voidptr, left);
		STACK_PUSHX(stack, int, ADDTAGS_RECURSE);

		/* Regset is not empty, so add a tag here. */
		if (regset[0] >= 0)
		  {
		    if (!first_pass)
		      {
			int i;
			status = tre_add_tag_left(mem, node, tag);
			tnfa->tag_directions[tag] = direction;
			if (minimal_tag >= 0)
			  {
			    for (i = 0; tnfa->minimal_tags[i] >= 0; i++);
			    tnfa->minimal_tags[i] = tag;
			    tnfa->minimal_tags[i + 1] = minimal_tag;
			    tnfa->minimal_tags[i + 2] = -1;
			    minimal_tag = -1;
			    num_minimals++;
			  }
			tre_purge_regset(regset, tnfa, tag);
		      }

		    regset[0] = -1;
		    tag = next_tag;
		    num_tags++;
		    next_tag++;
		  }

		if (node->num_submatches > 0)
		  {
		    /* The next two tags are reserved for markers. */
		    next_tag++;
		    tag = next_tag;
		    next_tag++;
		  }

		break;
	      }
	    }

	  if (node->submatch_id >= 0)
	    {
	      int i;
	      /* Push this submatch on the parents stack. */
	      for (i = 0; parents[i] >= 0; i++);
	      parents[i] = node->submatch_id;
	      parents[i + 1] = -1;
	    }

	  break; /* end case: ADDTAGS_RECURSE */

	case ADDTAGS_AFTER_ITERATION:
	  {
	    int minimal = 0;
	    int enter_tag;
	    node = tre_stack_pop_voidptr(stack);
	    if (first_pass)
	      {
		node->num_tags = ((tre_iteration_t *)node->obj)->arg->num_tags
		  + tre_stack_pop_int(stack);
		minimal_tag = -1;
	      }
	    else
	      {
		minimal = tre_stack_pop_int(stack);
		enter_tag = tre_stack_pop_int(stack);
		if (minimal)
		  minimal_tag = enter_tag;
	      }

	    if (!first_pass)
	      {
		if (minimal)
		  direction = TRE_TAG_MINIMIZE;
		else
		  direction = TRE_TAG_MAXIMIZE;
	      }
	    break;
	  }

	case ADDTAGS_AFTER_CAT_LEFT:
	  {
	    int new_tag = tre_stack_pop_int(stack);
	    next_tag = tre_stack_pop_int(stack);
	    if (new_tag >= 0)
	      {
		tag = new_tag;
	      }
	    break;
	  }

	case ADDTAGS_AFTER_CAT_RIGHT:
	  node = tre_stack_pop_voidptr(stack);
	  if (first_pass)
	    node->num_tags = ((tre_catenation_t *)node->obj)->left->num_tags
	      + ((tre_catenation_t *)node->obj)->right->num_tags;
	  break;

	case ADDTAGS_AFTER_UNION_LEFT:
	  /* Lift the bottom of the `regset' array so that when processing
	     the right operand the items currently in the array are
	     invisible.	 The original bottom was saved at ADDTAGS_UNION and
	     will be restored at ADDTAGS_AFTER_UNION_RIGHT below. */
	  while (*regset >= 0)
	    regset++;
	  break;

	case ADDTAGS_AFTER_UNION_RIGHT:
	  {
	    int added_tags, tag_left, tag_right;
	    tre_ast_node_t *left = tre_stack_pop_voidptr(stack);
	    tre_ast_node_t *right = tre_stack_pop_voidptr(stack);
	    node = tre_stack_pop_voidptr(stack);
	    added_tags = tre_stack_pop_int(stack);
	    if (first_pass)
	      {
		node->num_tags = ((tre_union_t *)node->obj)->left->num_tags
		  + ((tre_union_t *)node->obj)->right->num_tags + added_tags
		  + ((node->num_submatches > 0) ? 2 : 0);
	      }
	    regset = tre_stack_pop_voidptr(stack);
	    tag_left = tre_stack_pop_int(stack);
	    tag_right = tre_stack_pop_int(stack);

	    /* Add tags after both children, the left child gets a smaller
	       tag than the right child.  This guarantees that we prefer
	       the left child over the right child. */
	    /* XXX - This is not always necessary (if the children have
	       tags which must be seen for every match of that child). */
	    /* XXX - Check if this is the only place where tre_add_tag_right
	       is used.	 If so, use tre_add_tag_left (putting the tag before
	       the child as opposed after the child) and throw away
	       tre_add_tag_right. */
	    if (node->num_submatches > 0)
	      {
		if (!first_pass)
		  {
		    status = tre_add_tag_right(mem, left, tag_left);
		    tnfa->tag_directions[tag_left] = TRE_TAG_MAXIMIZE;
		    if (status == REG_OK)
		      status = tre_add_tag_right(mem, right, tag_right);
		    tnfa->tag_directions[tag_right] = TRE_TAG_MAXIMIZE;
		  }
		num_tags += 2;
	      }
	    direction = TRE_TAG_MAXIMIZE;
	    break;
	  }

	default:
	  assert(0);
	  break;

	} /* end switch(symbol) */
    } /* end while(tre_stack_num_objects(stack) > bottom) */

  if (!first_pass)
    tre_purge_regset(regset, tnfa, tag);

  if (!first_pass && minimal_tag >= 0)
    {
      int i;
      for (i = 0; tnfa->minimal_tags[i] >= 0; i++);
      tnfa->minimal_tags[i] = tag;
      tnfa->minimal_tags[i + 1] = minimal_tag;
      tnfa->minimal_tags[i + 2] = -1;
      minimal_tag = -1;
      num_minimals++;
    }

  assert(tree->num_tags == num_tags);
  tnfa->end_tag = num_tags;
  tnfa->num_tags = num_tags;
  tnfa->num_minimals = num_minimals;
  xfree(orig_regset);
  xfree(parents);
  xfree(saved_states);
  return status;
}

static reg_errcode_t tre_copy_ast(tre_mem_t mem, tre_stack_t *stack, tre_ast_node_t *ast,
	     int flags, int *pos_add, tre_tag_direction_t *tag_directions,
	     tre_ast_node_t **copy, int *max_pos)
{
  reg_errcode_t status = REG_OK;
  int bottom = tre_stack_num_objects(stack);
  int num_copied = 0;
  int first_tag = 1;
  tre_ast_node_t **result = copy;
  tre_copyast_symbol_t symbol;

  STACK_PUSH(stack, voidptr, ast);
  STACK_PUSH(stack, int, COPY_RECURSE);

  while (status == REG_OK && tre_stack_num_objects(stack) > bottom)
    {
      tre_ast_node_t *node;
      if (status != REG_OK)
	break;

      symbol = (tre_copyast_symbol_t)tre_stack_pop_int(stack);
      switch (symbol)
	{
	case COPY_SET_RESULT_PTR:
	  result = tre_stack_pop_voidptr(stack);
	  break;
	case COPY_RECURSE:
	  node = tre_stack_pop_voidptr(stack);
	  switch (node->type)
	    {
	    case LITERAL:
	      {
		tre_literal_t *lit = node->obj;
		int pos = lit->position;
		int min = lit->code_min;
		int max = lit->code_max;
		if (!IS_SPECIAL(lit) || IS_BACKREF(lit))
		  {
		    /* XXX - e.g. [ab] has only one position but two
		       nodes, so we are creating holes in the state space
		       here.  Not fatal, just wastes memory. */
		    pos += *pos_add;
		    num_copied++;
		  }
		else if (IS_TAG(lit) && (flags & COPY_REMOVE_TAGS))
		  {
		    /* Change this tag to empty. */
		    min = EMPTY;
		    max = pos = -1;
		  }
		else if (IS_TAG(lit) && (flags & COPY_MAXIMIZE_FIRST_TAG)
			 && first_tag)
		  {
		    /* Maximize the first tag. */
		    tag_directions[max] = TRE_TAG_MAXIMIZE;
		    first_tag = 0;
		  }
		*result = tre_ast_new_literal(mem, min, max, pos);
		if (*result == NULL)
		  status = REG_ESPACE;
		else {
		  tre_literal_t *p = (*result)->obj;
		  p->class = lit->class;
		  p->neg_classes = lit->neg_classes;
		}

		if (pos > *max_pos)
		  *max_pos = pos;
		break;
	      }
	    case UNION:
	      {
		tre_union_t *uni = node->obj;
		tre_union_t *tmp;
		*result = tre_ast_new_union(mem, uni->left, uni->right);
		if (*result == NULL)
		  {
		    status = REG_ESPACE;
		    break;
		  }
		tmp = (*result)->obj;
		result = &tmp->left;
		STACK_PUSHX(stack, voidptr, uni->right);
		STACK_PUSHX(stack, int, COPY_RECURSE);
		STACK_PUSHX(stack, voidptr, &tmp->right);
		STACK_PUSHX(stack, int, COPY_SET_RESULT_PTR);
		STACK_PUSHX(stack, voidptr, uni->left);
		STACK_PUSHX(stack, int, COPY_RECURSE);
		break;
	      }
	    case CATENATION:
	      {
		tre_catenation_t *cat = node->obj;
		tre_catenation_t *tmp;
		*result = tre_ast_new_catenation(mem, cat->left, cat->right);
		if (*result == NULL)
		  {
		    status = REG_ESPACE;
		    break;
		  }
		tmp = (*result)->obj;
		tmp->left = NULL;
		tmp->right = NULL;
		result = &tmp->left;

		STACK_PUSHX(stack, voidptr, cat->right);
		STACK_PUSHX(stack, int, COPY_RECURSE);
		STACK_PUSHX(stack, voidptr, &tmp->right);
		STACK_PUSHX(stack, int, COPY_SET_RESULT_PTR);
		STACK_PUSHX(stack, voidptr, cat->left);
		STACK_PUSHX(stack, int, COPY_RECURSE);
		break;
	      }
	    case ITERATION:
	      {
		tre_iteration_t *iter = node->obj;
		STACK_PUSHX(stack, voidptr, iter->arg);
		STACK_PUSHX(stack, int, COPY_RECURSE);
		*result = tre_ast_new_iter(mem, iter->arg, iter->min,
					   iter->max, iter->minimal);
		if (*result == NULL)
		  {
		    status = REG_ESPACE;
		    break;
		  }
		iter = (*result)->obj;
		result = &iter->arg;
		break;
	      }
	    default:
	      assert(0);
	      break;
	    }
	  break;
	}
    }
  *pos_add += num_copied;
  return status;
}

static reg_errcode_t tre_expand_ast(tre_mem_t mem, tre_stack_t *stack, tre_ast_node_t *ast,
	       int *position, tre_tag_direction_t *tag_directions)
{
  reg_errcode_t status = REG_OK;
  int bottom = tre_stack_num_objects(stack);
  int pos_add = 0;
  int pos_add_total = 0;
  int max_pos = 0;
  int iter_depth = 0;

  STACK_PUSHR(stack, voidptr, ast);
  STACK_PUSHR(stack, int, EXPAND_RECURSE);
  while (status == REG_OK && tre_stack_num_objects(stack) > bottom)
    {
      tre_ast_node_t *node;
      tre_expand_ast_symbol_t symbol;

      if (status != REG_OK)
	break;

      symbol = (tre_expand_ast_symbol_t)tre_stack_pop_int(stack);
      node = tre_stack_pop_voidptr(stack);
      switch (symbol)
	{
	case EXPAND_RECURSE:
	  switch (node->type)
	    {
	    case LITERAL:
	      {
		tre_literal_t *lit= node->obj;
		if (!IS_SPECIAL(lit) || IS_BACKREF(lit))
		  {
		    lit->position += pos_add;
		    if (lit->position > max_pos)
		      max_pos = lit->position;
		  }
		break;
	      }
	    case UNION:
	      {
		tre_union_t *uni = node->obj;
		STACK_PUSHX(stack, voidptr, uni->right);
		STACK_PUSHX(stack, int, EXPAND_RECURSE);
		STACK_PUSHX(stack, voidptr, uni->left);
		STACK_PUSHX(stack, int, EXPAND_RECURSE);
		break;
	      }
	    case CATENATION:
	      {
		tre_catenation_t *cat = node->obj;
		STACK_PUSHX(stack, voidptr, cat->right);
		STACK_PUSHX(stack, int, EXPAND_RECURSE);
		STACK_PUSHX(stack, voidptr, cat->left);
		STACK_PUSHX(stack, int, EXPAND_RECURSE);
		break;
	      }
	    case ITERATION:
	      {
		tre_iteration_t *iter = node->obj;
		STACK_PUSHX(stack, int, pos_add);
		STACK_PUSHX(stack, voidptr, node);
		STACK_PUSHX(stack, int, EXPAND_AFTER_ITER);
		STACK_PUSHX(stack, voidptr, iter->arg);
		STACK_PUSHX(stack, int, EXPAND_RECURSE);
		/* If we are going to expand this node at EXPAND_AFTER_ITER
		   then don't increase the `pos' fields of the nodes now, it
		   will get done when expanding. */
		if (iter->min > 1 || iter->max > 1)
		  pos_add = 0;
		iter_depth++;
		break;
	      }
	    default:
	      assert(0);
	      break;
	    }
	  break;
	case EXPAND_AFTER_ITER:
	  {
	    tre_iteration_t *iter = node->obj;
	    int pos_add_last;
	    pos_add = tre_stack_pop_int(stack);
	    pos_add_last = pos_add;
	    if (iter->min > 1 || iter->max > 1)
	      {
		tre_ast_node_t *seq1 = NULL, *seq2 = NULL;
		int j;
		int pos_add_save = pos_add;

		/* Create a catenated sequence of copies of the node. */
		for (j = 0; j < iter->min; j++)
		  {
		    tre_ast_node_t *copy;
		    /* Remove tags from all but the last copy. */
		    int flags = ((j + 1 < iter->min)
				 ? COPY_REMOVE_TAGS
				 : COPY_MAXIMIZE_FIRST_TAG);
		    pos_add_save = pos_add;
		    status = tre_copy_ast(mem, stack, iter->arg, flags,
					  &pos_add, tag_directions, &copy,
					  &max_pos);
		    if (status != REG_OK)
		      return status;
		    if (seq1 != NULL)
		      seq1 = tre_ast_new_catenation(mem, seq1, copy);
		    else
		      seq1 = copy;
		    if (seq1 == NULL)
		      return REG_ESPACE;
		  }

		if (iter->max == -1)
		  {
		    /* No upper limit. */
		    pos_add_save = pos_add;
		    status = tre_copy_ast(mem, stack, iter->arg, 0,
					  &pos_add, NULL, &seq2, &max_pos);
		    if (status != REG_OK)
		      return status;
		    seq2 = tre_ast_new_iter(mem, seq2, 0, -1, 0);
		    if (seq2 == NULL)
		      return REG_ESPACE;
		  }
		else
		  {
		    for (j = iter->min; j < iter->max; j++)
		      {
			tre_ast_node_t *tmp, *copy;
			pos_add_save = pos_add;
			status = tre_copy_ast(mem, stack, iter->arg, 0,
					      &pos_add, NULL, &copy, &max_pos);
			if (status != REG_OK)
			  return status;
			if (seq2 != NULL)
			  seq2 = tre_ast_new_catenation(mem, copy, seq2);
			else
			  seq2 = copy;
			if (seq2 == NULL)
			  return REG_ESPACE;
			tmp = tre_ast_new_literal(mem, EMPTY, -1, -1);
			if (tmp == NULL)
			  return REG_ESPACE;
			seq2 = tre_ast_new_union(mem, tmp, seq2);
			if (seq2 == NULL)
			  return REG_ESPACE;
		      }
		  }

		pos_add = pos_add_save;
		if (seq1 == NULL)
		  seq1 = seq2;
		else if (seq2 != NULL)
		  seq1 = tre_ast_new_catenation(mem, seq1, seq2);
		if (seq1 == NULL)
		  return REG_ESPACE;
		node->obj = seq1->obj;
		node->type = seq1->type;
	      }

	    iter_depth--;
	    pos_add_total += pos_add - pos_add_last;
	    if (iter_depth == 0)
	      pos_add = pos_add_total;

	    break;
	  }
	default:
	  assert(0);
	  break;
	}
    }

  *position += pos_add_total;

  /* `max_pos' should never be larger than `*position' if the above
     code works, but just an extra safeguard let's make sure
     `*position' is set large enough so enough memory will be
     allocated for the transition table. */
  if (max_pos > *position)
    *position = max_pos;

  return status;
}

static tre_pos_and_tags_t * tre_set_one(tre_mem_t mem, int position, int code_min, int code_max,
	    tre_ctype_t class, tre_ctype_t *neg_classes, int backref)
{
  tre_pos_and_tags_t *new_set;

  new_set = tre_mem_calloc(mem, sizeof(*new_set) * 2);
  if (new_set == NULL)
    return NULL;

  new_set[0].position = position;
  new_set[0].code_min = code_min;
  new_set[0].code_max = code_max;
  new_set[0].class = class;
  new_set[0].neg_classes = neg_classes;
  new_set[0].backref = backref;
  new_set[1].position = -1;
  new_set[1].code_min = -1;
  new_set[1].code_max = -1;

  return new_set;
}

static tre_pos_and_tags_t * tre_set_empty(tre_mem_t mem)
{
  tre_pos_and_tags_t *new_set;

  new_set = tre_mem_calloc(mem, sizeof(*new_set));
  if (new_set == NULL)
    return NULL;

  new_set[0].position = -1;
  new_set[0].code_min = -1;
  new_set[0].code_max = -1;

  return new_set;
}

static tre_pos_and_tags_t * tre_set_union(tre_mem_t mem, tre_pos_and_tags_t *set1, tre_pos_and_tags_t *set2,
	      int *tags, int assertions)
{
  int s1, s2, i, j;
  tre_pos_and_tags_t *new_set;
  int *new_tags;
  int num_tags;

  for (num_tags = 0; tags != NULL && tags[num_tags] >= 0; num_tags++);
  for (s1 = 0; set1[s1].position >= 0; s1++);
  for (s2 = 0; set2[s2].position >= 0; s2++);
  new_set = tre_mem_calloc(mem, sizeof(*new_set) * (s1 + s2 + 1));
  if (!new_set )
    return NULL;

  for (s1 = 0; set1[s1].position >= 0; s1++)
    {
      new_set[s1].position = set1[s1].position;
      new_set[s1].code_min = set1[s1].code_min;
      new_set[s1].code_max = set1[s1].code_max;
      new_set[s1].assertions = set1[s1].assertions | assertions;
      new_set[s1].class = set1[s1].class;
      new_set[s1].neg_classes = set1[s1].neg_classes;
      new_set[s1].backref = set1[s1].backref;
      if (set1[s1].tags == NULL && tags == NULL)
	new_set[s1].tags = NULL;
      else
	{
	  for (i = 0; set1[s1].tags != NULL && set1[s1].tags[i] >= 0; i++);
	  new_tags = tre_mem_alloc(mem, (sizeof(*new_tags)
					 * (i + num_tags + 1)));
	  if (new_tags == NULL)
	    return NULL;
	  for (j = 0; j < i; j++)
	    new_tags[j] = set1[s1].tags[j];
	  for (i = 0; i < num_tags; i++)
	    new_tags[j + i] = tags[i];
	  new_tags[j + i] = -1;
	  new_set[s1].tags = new_tags;
	}
    }

  for (s2 = 0; set2[s2].position >= 0; s2++)
    {
      new_set[s1 + s2].position = set2[s2].position;
      new_set[s1 + s2].code_min = set2[s2].code_min;
      new_set[s1 + s2].code_max = set2[s2].code_max;
      /* XXX - why not | assertions here as well? */
      new_set[s1 + s2].assertions = set2[s2].assertions;
      new_set[s1 + s2].class = set2[s2].class;
      new_set[s1 + s2].neg_classes = set2[s2].neg_classes;
      new_set[s1 + s2].backref = set2[s2].backref;
      if (set2[s2].tags == NULL)
	new_set[s1 + s2].tags = NULL;
      else
	{
	  for (i = 0; set2[s2].tags[i] >= 0; i++);
	  new_tags = tre_mem_alloc(mem, sizeof(*new_tags) * (i + 1));
	  if (new_tags == NULL)
	    return NULL;
	  for (j = 0; j < i; j++)
	    new_tags[j] = set2[s2].tags[j];
	  new_tags[j] = -1;
	  new_set[s1 + s2].tags = new_tags;
	}
    }
  new_set[s1 + s2].position = -1;
  return new_set;
}

static reg_errcode_t tre_match_empty(tre_stack_t *stack, tre_ast_node_t *node, int *tags,
		int *assertions, int *num_tags_seen)
{
  tre_literal_t *lit;
  tre_union_t *uni;
  tre_catenation_t *cat;
  tre_iteration_t *iter;
  int i;
  int bottom = tre_stack_num_objects(stack);
  reg_errcode_t status = REG_OK;
  if (num_tags_seen)
    *num_tags_seen = 0;

  status = tre_stack_push_voidptr(stack, node);

  /* Walk through the tree recursively. */
  while (status == REG_OK && tre_stack_num_objects(stack) > bottom)
    {
      node = tre_stack_pop_voidptr(stack);

      switch (node->type)
	{
	case LITERAL:
	  lit = (tre_literal_t *)node->obj;
	  switch (lit->code_min)
	    {
	    case TAG:
	      if (lit->code_max >= 0)
		{
		  if (tags != NULL)
		    {
		      /* Add the tag to `tags'. */
		      for (i = 0; tags[i] >= 0; i++)
			if (tags[i] == lit->code_max)
			  break;
		      if (tags[i] < 0)
			{
			  tags[i] = lit->code_max;
			  tags[i + 1] = -1;
			}
		    }
		  if (num_tags_seen)
		    (*num_tags_seen)++;
		}
	      break;
	    case ASSERTION:
	      assert(lit->code_max >= 1
		     || lit->code_max <= ASSERT_LAST);
	      if (assertions != NULL)
		*assertions |= lit->code_max;
	      break;
	    case EMPTY:
	      break;
	    default:
	      assert(0);
	      break;
	    }
	  break;

	case UNION:
	  /* Subexpressions starting earlier take priority over ones
	     starting later, so we prefer the left subexpression over the
	     right subexpression. */
	  uni = (tre_union_t *)node->obj;
	  if (uni->left->nullable)
	    STACK_PUSHX(stack, voidptr, uni->left)
	  else if (uni->right->nullable)
	    STACK_PUSHX(stack, voidptr, uni->right)
	  else
	    assert(0);
	  break;

	case CATENATION:
	  /* The path must go through both children. */
	  cat = (tre_catenation_t *)node->obj;
	  assert(cat->left->nullable);
	  assert(cat->right->nullable);
	  STACK_PUSHX(stack, voidptr, cat->left);
	  STACK_PUSHX(stack, voidptr, cat->right);
	  break;

	case ITERATION:
	  /* A match with an empty string is preferred over no match at
	     all, so we go through the argument if possible. */
	  iter = (tre_iteration_t *)node->obj;
	  if (iter->arg->nullable)
	    STACK_PUSHX(stack, voidptr, iter->arg);
	  break;

	default:
	  assert(0);
	  break;
	}
    }

  return status;
}

static reg_errcode_t tre_compute_nfl(tre_mem_t mem, tre_stack_t *stack, tre_ast_node_t *tree)
{
  int bottom = tre_stack_num_objects(stack);

  STACK_PUSHR(stack, voidptr, tree);
  STACK_PUSHR(stack, int, NFL_RECURSE);

  while (tre_stack_num_objects(stack) > bottom)
    {
      tre_nfl_stack_symbol_t symbol;
      tre_ast_node_t *node;

      symbol = (tre_nfl_stack_symbol_t)tre_stack_pop_int(stack);
      node = tre_stack_pop_voidptr(stack);
      switch (symbol)
	{
	case NFL_RECURSE:
	  switch (node->type)
	    {
	    case LITERAL:
	      {
		tre_literal_t *lit = (tre_literal_t *)node->obj;
		if (IS_BACKREF(lit))
		  {
		    /* Back references: nullable = false, firstpos = {i},
		       lastpos = {i}. */
		    node->nullable = 0;
		    node->firstpos = tre_set_one(mem, lit->position, 0,
					     TRE_CHAR_MAX, 0, NULL, -1);
		    if (!node->firstpos)
		      return REG_ESPACE;
		    node->lastpos = tre_set_one(mem, lit->position, 0,
						TRE_CHAR_MAX, 0, NULL,
						(int)lit->code_max);
		    if (!node->lastpos)
		      return REG_ESPACE;
		  }
		else if (lit->code_min < 0)
		  {
		    /* Tags, empty strings, params, and zero width assertions:
		       nullable = true, firstpos = {}, and lastpos = {}. */
		    node->nullable = 1;
		    node->firstpos = tre_set_empty(mem);
		    if (!node->firstpos)
		      return REG_ESPACE;
		    node->lastpos = tre_set_empty(mem);
		    if (!node->lastpos)
		      return REG_ESPACE;
		  }
		else
		  {
		    /* Literal at position i: nullable = false, firstpos = {i},
		       lastpos = {i}. */
		    node->nullable = 0;
		    node->firstpos =
		      tre_set_one(mem, lit->position, (int)lit->code_min,
				  (int)lit->code_max, 0, NULL, -1);
		    if (!node->firstpos)
		      return REG_ESPACE;
		    node->lastpos = tre_set_one(mem, lit->position,
						(int)lit->code_min,
						(int)lit->code_max,
						lit->class, lit->neg_classes,
						-1);
		    if (!node->lastpos)
		      return REG_ESPACE;
		  }
		break;
	      }

	    case UNION:
	      /* Compute the attributes for the two subtrees, and after that
		 for this node. */
	      STACK_PUSHR(stack, voidptr, node);
	      STACK_PUSHR(stack, int, NFL_POST_UNION);
	      STACK_PUSHR(stack, voidptr, ((tre_union_t *)node->obj)->right);
	      STACK_PUSHR(stack, int, NFL_RECURSE);
	      STACK_PUSHR(stack, voidptr, ((tre_union_t *)node->obj)->left);
	      STACK_PUSHR(stack, int, NFL_RECURSE);
	      break;

	    case CATENATION:
	      /* Compute the attributes for the two subtrees, and after that
		 for this node. */
	      STACK_PUSHR(stack, voidptr, node);
	      STACK_PUSHR(stack, int, NFL_POST_CATENATION);
	      STACK_PUSHR(stack, voidptr, ((tre_catenation_t *)node->obj)->right);
	      STACK_PUSHR(stack, int, NFL_RECURSE);
	      STACK_PUSHR(stack, voidptr, ((tre_catenation_t *)node->obj)->left);
	      STACK_PUSHR(stack, int, NFL_RECURSE);
	      break;

	    case ITERATION:
	      /* Compute the attributes for the subtree, and after that for
		 this node. */
	      STACK_PUSHR(stack, voidptr, node);
	      STACK_PUSHR(stack, int, NFL_POST_ITERATION);
	      STACK_PUSHR(stack, voidptr, ((tre_iteration_t *)node->obj)->arg);
	      STACK_PUSHR(stack, int, NFL_RECURSE);
	      break;
	    }
	  break; /* end case: NFL_RECURSE */

	case NFL_POST_UNION:
	  {
	    tre_union_t *uni = (tre_union_t *)node->obj;
	    node->nullable = uni->left->nullable || uni->right->nullable;
	    node->firstpos = tre_set_union(mem, uni->left->firstpos,
					   uni->right->firstpos, NULL, 0);
	    if (!node->firstpos)
	      return REG_ESPACE;
	    node->lastpos = tre_set_union(mem, uni->left->lastpos,
					  uni->right->lastpos, NULL, 0);
	    if (!node->lastpos)
	      return REG_ESPACE;
	    break;
	  }

	case NFL_POST_ITERATION:
	  {
	    tre_iteration_t *iter = (tre_iteration_t *)node->obj;

	    if (iter->min == 0 || iter->arg->nullable)
	      node->nullable = 1;
	    else
	      node->nullable = 0;
	    node->firstpos = iter->arg->firstpos;
	    node->lastpos = iter->arg->lastpos;
	    break;
	  }

	case NFL_POST_CATENATION:
	  {
	    int num_tags, *tags, assertions;
	    reg_errcode_t status;
	    tre_catenation_t *cat = node->obj;
	    node->nullable = cat->left->nullable && cat->right->nullable;

	    /* Compute firstpos. */
	    if (cat->left->nullable)
	      {
		/* The left side matches the empty string.  Make a first pass
		   with tre_match_empty() to get the number of tags and
		   parameters. */
		status = tre_match_empty(stack, cat->left,
					 NULL, NULL, &num_tags);
		if (status != REG_OK)
		  return status;
		/* Allocate arrays for the tags and parameters. */
		tags = xmalloc(sizeof(*tags) * (num_tags + 1));
		if (!tags)
		  return REG_ESPACE;
		tags[0] = -1;
		assertions = 0;
		/* Second pass with tre_mach_empty() to get the list of
		   tags and parameters. */
		status = tre_match_empty(stack, cat->left, tags,
					 &assertions, NULL);
		if (status != REG_OK)
		  {
		    xfree(tags);
		    return status;
		  }
		node->firstpos =
		  tre_set_union(mem, cat->right->firstpos, cat->left->firstpos,
				tags, assertions);
		xfree(tags);
		if (!node->firstpos)
		  return REG_ESPACE;
	      }
	    else
	      {
		node->firstpos = cat->left->firstpos;
	      }

	    /* Compute lastpos. */
	    if (cat->right->nullable)
	      {
		/* The right side matches the empty string.  Make a first pass
		   with tre_match_empty() to get the number of tags and
		   parameters. */
		status = tre_match_empty(stack, cat->right,
					 NULL, NULL, &num_tags);
		if (status != REG_OK)
		  return status;
		/* Allocate arrays for the tags and parameters. */
		tags = xmalloc(sizeof(int) * (num_tags + 1));
		if (!tags)
		  return REG_ESPACE;
		tags[0] = -1;
		assertions = 0;
		/* Second pass with tre_mach_empty() to get the list of
		   tags and parameters. */
		status = tre_match_empty(stack, cat->right, tags,
					 &assertions, NULL);
		if (status != REG_OK)
		  {
		    xfree(tags);
		    return status;
		  }
		node->lastpos =
		  tre_set_union(mem, cat->left->lastpos, cat->right->lastpos,
				tags, assertions);
		xfree(tags);
		if (!node->lastpos)
		  return REG_ESPACE;
	      }
	    else
	      {
		node->lastpos = cat->right->lastpos;
	      }
	    break;
	  }

	default:
	  assert(0);
	  break;
	}
    }

  return REG_OK;
}

static reg_errcode_t
tre_make_trans(tre_pos_and_tags_t *p1, tre_pos_and_tags_t *p2,
	       tre_tnfa_transition_t *transitions,
	       int *counts, int *offs)
{
  tre_pos_and_tags_t *orig_p2 = p2;
  tre_tnfa_transition_t *trans;
  int i, j, k, l, dup, prev_p2_pos;

  if (transitions != NULL)
    while (p1->position >= 0)
      {
	p2 = orig_p2;
	prev_p2_pos = -1;
	while (p2->position >= 0)
	  {
	    /* Optimization: if this position was already handled, skip it. */
	    if (p2->position == prev_p2_pos)
	      {
		p2++;
		continue;
	      }
	    prev_p2_pos = p2->position;
	    /* Set `trans' to point to the next unused transition from
	       position `p1->position'. */
	    trans = transitions + offs[p1->position];
	    while (trans->state != NULL)
	      {
#if 0
		/* If we find a previous transition from `p1->position' to
		   `p2->position', it is overwritten.  This can happen only
		   if there are nested loops in the regexp, like in "((a)*)*".
		   In POSIX.2 repetition using the outer loop is always
		   preferred over using the inner loop.	 Therefore the
		   transition for the inner loop is useless and can be thrown
		   away. */
		/* XXX - The same position is used for all nodes in a bracket
		   expression, so this optimization cannot be used (it will
		   break bracket expressions) unless I figure out a way to
		   detect it here. */
		if (trans->state_id == p2->position)
		  {
		    break;
		  }
#endif
		trans++;
	      }

	    if (trans->state == NULL)
	      (trans + 1)->state = NULL;
	    /* Use the character ranges, assertions, etc. from `p1' for
	       the transition from `p1' to `p2'. */
	    trans->code_min = (tre_cint_t)p1->code_min;
	    trans->code_max = (tre_cint_t)p1->code_max;
	    trans->state = transitions + offs[p2->position];
	    trans->state_id = p2->position;
	    trans->assertions = p1->assertions | p2->assertions
	      | (p1->class ? ASSERT_CHAR_CLASS : 0)
	      | (p1->neg_classes != NULL ? ASSERT_CHAR_CLASS_NEG : 0);
	    if (p1->backref >= 0)
	      {
		assert((trans->assertions & ASSERT_CHAR_CLASS) == 0);
		assert(p2->backref < 0);
		trans->u.backref = p1->backref;
		trans->assertions |= ASSERT_BACKREF;
	      }
	    else
	      trans->u.class = p1->class;
	    if (p1->neg_classes != NULL)
	      {
		for (i = 0; p1->neg_classes[i] != (tre_ctype_t)0; i++);
		trans->neg_classes =
		  xmalloc(sizeof(*trans->neg_classes) * (i + 1));
		if (trans->neg_classes == NULL)
		  return REG_ESPACE;
		for (i = 0; p1->neg_classes[i] != (tre_ctype_t)0; i++)
		  trans->neg_classes[i] = p1->neg_classes[i];
		trans->neg_classes[i] = (tre_ctype_t)0;
	      }
	    else
	      trans->neg_classes = NULL;

	    /* Find out how many tags this transition has. */
	    i = 0;
	    if (p1->tags != NULL)
	      while(p1->tags[i] >= 0)
		i++;
	    j = 0;
	    if (p2->tags != NULL)
	      while(p2->tags[j] >= 0)
		j++;

	    /* If we are overwriting a transition, free the old tag array. */
	    if (trans->tags != NULL)
	      xfree(trans->tags);
	    trans->tags = NULL;

	    /* If there were any tags, allocate an array and fill it. */
	    if (i + j > 0)
	      {
		trans->tags = xmalloc(sizeof(*trans->tags) * (i + j + 1));
		if (!trans->tags)
		  return REG_ESPACE;
		i = 0;
		if (p1->tags != NULL)
		  while(p1->tags[i] >= 0)
		    {
		      trans->tags[i] = p1->tags[i];
		      i++;
		    }
		l = i;
		j = 0;
		if (p2->tags != NULL)
		  while (p2->tags[j] >= 0)
		    {
		      /* Don't add duplicates. */
		      dup = 0;
		      for (k = 0; k < i; k++)
			if (trans->tags[k] == p2->tags[j])
			  {
			    dup = 1;
			    break;
			  }
		      if (!dup)
			trans->tags[l++] = p2->tags[j];
		      j++;
		    }
		trans->tags[l] = -1;
	      }

	    p2++;
	  }
	p1++;
      }
  else
    /* Compute a maximum limit for the number of transitions leaving
       from each state. */
    while (p1->position >= 0)
      {
	p2 = orig_p2;
	while (p2->position >= 0)
	  {
	    counts[p1->position]++;
	    p2++;
	  }
	p1++;
      }
  return REG_OK;
}

static reg_errcode_t
tre_ast_to_tnfa(tre_ast_node_t *node, tre_tnfa_transition_t *transitions,
		int *counts, int *offs)
{
  tre_union_t *uni;
  tre_catenation_t *cat;
  tre_iteration_t *iter;
  reg_errcode_t errcode = REG_OK;

  /* XXX - recurse using a stack!. */
  switch (node->type)
    {
    case LITERAL:
      break;
    case UNION:
      uni = (tre_union_t *)node->obj;
      errcode = tre_ast_to_tnfa(uni->left, transitions, counts, offs);
      if (errcode != REG_OK)
	return errcode;
      errcode = tre_ast_to_tnfa(uni->right, transitions, counts, offs);
      break;

    case CATENATION:
      cat = (tre_catenation_t *)node->obj;
      /* Add a transition from each position in cat->left->lastpos
	 to each position in cat->right->firstpos. */
      errcode = tre_make_trans(cat->left->lastpos, cat->right->firstpos,
			       transitions, counts, offs);
      if (errcode != REG_OK)
	return errcode;
      errcode = tre_ast_to_tnfa(cat->left, transitions, counts, offs);
      if (errcode != REG_OK)
	return errcode;
      errcode = tre_ast_to_tnfa(cat->right, transitions, counts, offs);
      break;

    case ITERATION:
      iter = (tre_iteration_t *)node->obj;
      assert(iter->max == -1 || iter->max == 1);

      if (iter->max == -1)
	{
	  assert(iter->min == 0 || iter->min == 1);
	  /* Add a transition from each last position in the iterated
	     expression to each first position. */
	  errcode = tre_make_trans(iter->arg->lastpos, iter->arg->firstpos,
				   transitions, counts, offs);
	  if (errcode != REG_OK)
	    return errcode;
	}
      errcode = tre_ast_to_tnfa(iter->arg, transitions, counts, offs);
      break;
    }
  return errcode;
}

static void
tre_mem_destroy(tre_mem_t mem)
{
  tre_list_t *tmp, *l = mem->blocks;

  while (l != NULL)
    {
      xfree(l->data);
      tmp = l->next;
      xfree(l);
      l = tmp;
    }
  xfree(mem);
}

static int
tre_tag_order(int num_tags, tre_tag_direction_t *tag_directions,
	      regoff_t *t1, regoff_t *t2)
{
  int i;
  for (i = 0; i < num_tags; i++)
    {
      if (tag_directions[i] == TRE_TAG_MINIMIZE)
	{
	  if (t1[i] < t2[i])
	    return 1;
	  if (t1[i] > t2[i])
	    return 0;
	}
      else
	{
	  if (t1[i] > t2[i])
	    return 1;
	  if (t1[i] < t2[i])
	    return 0;
	}
    }
  /*  assert(0);*/
  return 0;
}

static void
tre_fill_pmatch(size_t nmatch, regmatch_t pmatch[], int cflags,
		const tre_tnfa_t *tnfa, regoff_t *tags, regoff_t match_eo)
{
  tre_submatch_data_t *submatch_data;
  unsigned int i, j;
  int *parents;

  i = 0;
  if (match_eo >= 0 && !(cflags & REG_NOSUB))
    {
      /* Construct submatch offsets from the tags. */
      submatch_data = tnfa->submatch_data;
      while (i < tnfa->num_submatches && i < nmatch)
	{
	  if (submatch_data[i].so_tag == tnfa->end_tag)
	    pmatch[i].rm_so = match_eo;
	  else
	    pmatch[i].rm_so = tags[submatch_data[i].so_tag];

	  if (submatch_data[i].eo_tag == tnfa->end_tag)
	    pmatch[i].rm_eo = match_eo;
	  else
	    pmatch[i].rm_eo = tags[submatch_data[i].eo_tag];

	  /* If either of the endpoints were not used, this submatch
	     was not part of the match. */
	  if (pmatch[i].rm_so == -1 || pmatch[i].rm_eo == -1)
	    pmatch[i].rm_so = pmatch[i].rm_eo = -1;

	  i++;
	}
      /* Reset all submatches that are not within all of their parent
	 submatches. */
      i = 0;
      while (i < tnfa->num_submatches && i < nmatch)
	{
	  if (pmatch[i].rm_eo == -1)
	    assert(pmatch[i].rm_so == -1);
	  assert(pmatch[i].rm_so <= pmatch[i].rm_eo);

	  parents = submatch_data[i].parents;
	  if (parents != NULL)
	    for (j = 0; parents[j] >= 0; j++)
	      {
		if (pmatch[i].rm_so < pmatch[parents[j]].rm_so
		    || pmatch[i].rm_eo > pmatch[parents[j]].rm_eo)
		  pmatch[i].rm_so = pmatch[i].rm_eo = -1;
	      }
	  i++;
	}
    }

  while (i < nmatch)
    {
      pmatch[i].rm_so = -1;
      pmatch[i].rm_eo = -1;
      i++;
    }
}

static int
tre_neg_char_classes_match(tre_ctype_t *classes, tre_cint_t wc, int icase)
{
  while (*classes != (tre_ctype_t)0)
    if ((!icase && tre_isctype(wc, *classes))
	|| (icase && (tre_isctype(tre_toupper(wc), *classes)
		      || tre_isctype(tre_tolower(wc), *classes))))
      return 1; /* Match. */
    else
      classes++;
  return 0; /* No match. */
}

static reg_errcode_t
tre_tnfa_run_backtrack(const tre_tnfa_t *tnfa, const void *string,
		       regoff_t *match_tags, int eflags, regoff_t *match_end_ofs)
{
  /* State variables required by GET_NEXT_WCHAR. */
  tre_char_t prev_c = 0, next_c = 0;
  const char *str_byte = string;
  regoff_t pos = 0;
  regoff_t pos_add_next = 1;
#ifdef TRE_MBSTATE
  mbstate_t mbstate;
#endif /* TRE_MBSTATE */
  int reg_notbol = eflags & REG_NOTBOL;
  int reg_noteol = eflags & REG_NOTEOL;
  int reg_newline = tnfa->cflags & REG_NEWLINE;

  /* These are used to remember the necessary values of the above
     variables to return to the position where the current search
     started from. */
  int next_c_start;
  const char *str_byte_start;
  regoff_t pos_start = -1;
#ifdef TRE_MBSTATE
  mbstate_t mbstate_start;
#endif /* TRE_MBSTATE */

  /* End offset of best match so far, or -1 if no match found yet. */
  regoff_t match_eo = -1;
  /* Tag arrays. */
  int *next_tags;
  regoff_t *tags = NULL;
  /* Current TNFA state. */
  tre_tnfa_transition_t *state;
  int *states_seen = NULL;

  /* Memory allocator to for allocating the backtracking stack. */
  tre_mem_t mem = tre_bt_mem_new();

  /* The backtracking stack. */
  tre_backtrack_t stack;

  tre_tnfa_transition_t *trans_i;
  regmatch_t *pmatch = NULL;
  int ret;

#ifdef TRE_MBSTATE
  memset(&mbstate, '\0', sizeof(mbstate));
#endif /* TRE_MBSTATE */

  if (!mem)
    return REG_ESPACE;
  stack = tre_bt_mem_alloc(mem, sizeof(*stack));
  if (!stack)
    {
      ret = REG_ESPACE;
      goto error_exit;
    }
  stack->prev = NULL;
  stack->next = NULL;

  if (tnfa->num_tags)
    {
      tags = xmalloc(sizeof(*tags) * tnfa->num_tags);
      if (!tags)
	{
	  ret = REG_ESPACE;
	  goto error_exit;
	}
    }
  if (tnfa->num_submatches)
    {
      pmatch = xmalloc(sizeof(*pmatch) * tnfa->num_submatches);
      if (!pmatch)
	{
	  ret = REG_ESPACE;
	  goto error_exit;
	}
    }
  if (tnfa->num_states)
    {
      states_seen = xmalloc(sizeof(*states_seen) * tnfa->num_states);
      if (!states_seen)
	{
	  ret = REG_ESPACE;
	  goto error_exit;
	}
    }

 retry:
  {
    int i;
    for (i = 0; i < tnfa->num_tags; i++)
      {
	tags[i] = -1;
	if (match_tags)
	  match_tags[i] = -1;
      }
    for (i = 0; i < tnfa->num_states; i++)
      states_seen[i] = 0;
  }

  state = NULL;
  pos = pos_start;
  GET_NEXT_WCHAR();
  pos_start = pos;
  next_c_start = next_c;
  str_byte_start = str_byte;
#ifdef TRE_MBSTATE
  mbstate_start = mbstate;
#endif /* TRE_MBSTATE */

  /* Handle initial states. */
  next_tags = NULL;
  for (trans_i = tnfa->initial; trans_i->state; trans_i++)
    {
      if (trans_i->assertions && CHECK_ASSERTIONS(trans_i->assertions))
	{
	  continue;
	}
      if (state == NULL)
	{
	  /* Start from this state. */
	  state = trans_i->state;
	  next_tags = trans_i->tags;
	}
      else
	{
	  /* Backtrack to this state. */
	  BT_STACK_PUSH(pos, str_byte, 0, trans_i->state,
			trans_i->state_id, next_c, tags, mbstate);
	  {
	    int *tmp = trans_i->tags;
	    if (tmp)
	      while (*tmp >= 0)
		stack->item.tags[*tmp++] = pos;
	  }
	}
    }

  if (next_tags)
    for (; *next_tags >= 0; next_tags++)
      tags[*next_tags] = pos;


  if (state == NULL)
    goto backtrack;

  while (1)
    {
      tre_tnfa_transition_t *next_state;
      int empty_br_match;

      if (state == tnfa->final)
	{
	  if (match_eo < pos
	      || (match_eo == pos
		  && match_tags
		  && tre_tag_order(tnfa->num_tags, tnfa->tag_directions,
				   tags, match_tags)))
	    {
	      int i;
	      /* This match wins the previous match. */
	      match_eo = pos;
	      if (match_tags)
		for (i = 0; i < tnfa->num_tags; i++)
		  match_tags[i] = tags[i];
	    }
	  /* Our TNFAs never have transitions leaving from the final state,
	     so we jump right to backtracking. */
	  goto backtrack;
	}

      /* Go to the next character in the input string. */
      empty_br_match = 0;
      trans_i = state;
      if (trans_i->state && trans_i->assertions & ASSERT_BACKREF)
	{
	  /* This is a back reference state.  All transitions leaving from
	     this state have the same back reference "assertion".  Instead
	     of reading the next character, we match the back reference. */
	  regoff_t so, eo;
	  int bt = trans_i->u.backref;
	  regoff_t bt_len;
	  int result;

	  /* Get the substring we need to match against.  Remember to
	     turn off REG_NOSUB temporarily. */
	  tre_fill_pmatch(bt + 1, pmatch, tnfa->cflags & ~REG_NOSUB,
			  tnfa, tags, pos);
	  so = pmatch[bt].rm_so;
	  eo = pmatch[bt].rm_eo;
	  bt_len = eo - so;

	  result = strncmp((const char*)string + so, str_byte - 1,
				 (size_t)bt_len);

	  if (result == 0)
	    {
	      /* Back reference matched.  Check for infinite loop. */
	      if (bt_len == 0)
		empty_br_match = 1;
	      if (empty_br_match && states_seen[trans_i->state_id])
		{
		  goto backtrack;
		}

	      states_seen[trans_i->state_id] = empty_br_match;

	      /* Advance in input string and resync `prev_c', `next_c'
		 and pos. */
	      str_byte += bt_len - 1;
	      pos += bt_len - 1;
	      GET_NEXT_WCHAR();
	    }
	  else
	    {
	      goto backtrack;
	    }
	}
      else
	{
	  /* Check for end of string. */
	  if (next_c == L'\0')
		goto backtrack;

	  /* Read the next character. */
	  GET_NEXT_WCHAR();
	}

      next_state = NULL;
      for (trans_i = state; trans_i->state; trans_i++)
	{
	  if (trans_i->code_min <= (tre_cint_t)prev_c
	      && trans_i->code_max >= (tre_cint_t)prev_c)
	    {
	      if (trans_i->assertions
		  && (CHECK_ASSERTIONS(trans_i->assertions)
		      || CHECK_CHAR_CLASSES(trans_i, tnfa, eflags)))
		{
		  continue;
		}

	      if (next_state == NULL)
		{
		  /* First matching transition. */
		  next_state = trans_i->state;
		  next_tags = trans_i->tags;
		}
	      else
		{
		  /* Second matching transition.  We may need to backtrack here
		     to take this transition instead of the first one, so we
		     push this transition in the backtracking stack so we can
		     jump back here if needed. */
		  BT_STACK_PUSH(pos, str_byte, 0, trans_i->state,
				trans_i->state_id, next_c, tags, mbstate);
		  {
		    int *tmp;
		    for (tmp = trans_i->tags; tmp && *tmp >= 0; tmp++)
		      stack->item.tags[*tmp] = pos;
		  }
#if 0 /* XXX - it's important not to look at all transitions here to keep
	 the stack small! */
		  break;
#endif
		}
	    }
	}

      if (next_state != NULL)
	{
	  /* Matching transitions were found.  Take the first one. */
	  state = next_state;

	  /* Update the tag values. */
	  if (next_tags)
	    while (*next_tags >= 0)
	      tags[*next_tags++] = pos;
	}
      else
	{
	backtrack:
	  /* A matching transition was not found.  Try to backtrack. */
	  if (stack->prev)
	    {
	      if (stack->item.state->assertions & ASSERT_BACKREF)
		{
		  states_seen[stack->item.state_id] = 0;
		}

	      BT_STACK_POP();
	    }
	  else if (match_eo < 0)
	    {
	      /* Try starting from a later position in the input string. */
	      /* Check for end of string. */
	      if (next_c == L'\0')
		    {
		      break;
		    }
	      next_c = (tre_char_t)next_c_start;
#ifdef TRE_MBSTATE
	      mbstate = mbstate_start;
#endif /* TRE_MBSTATE */
	      str_byte = str_byte_start;
	      goto retry;
	    }
	  else
	    {
	      break;
	    }
	}
    }

  ret = match_eo >= 0 ? REG_OK : REG_NOMATCH;
  *match_end_ofs = match_eo;

 error_exit:
  tre_bt_mem_destroy(mem);
#ifndef TRE_USE_ALLOCA
  if (tags)
    xfree(tags);
  if (pmatch)
    xfree(pmatch);
  if (states_seen)
    xfree(states_seen);
#endif /* !TRE_USE_ALLOCA */

  return ret;
}

static reg_errcode_t
tre_tnfa_run_parallel(const tre_tnfa_t *tnfa, const void *string,
		      regoff_t *match_tags, int eflags,
		      regoff_t *match_end_ofs)
{
  /* State variables required by GET_NEXT_WCHAR. */
  tre_char_t prev_c = 0, next_c = 0;
  const char *str_byte = string;
  regoff_t pos = -1;
  regoff_t pos_add_next = 1;
#ifdef TRE_MBSTATE
  mbstate_t mbstate;
#endif /* TRE_MBSTATE */
  int reg_notbol = eflags & REG_NOTBOL;
  int reg_noteol = eflags & REG_NOTEOL;
  int reg_newline = tnfa->cflags & REG_NEWLINE;
  reg_errcode_t ret;

  char *buf;
  tre_tnfa_transition_t *trans_i;
  tre_tnfa_reach_t *reach, *reach_next, *reach_i, *reach_next_i;
  tre_reach_pos_t *reach_pos;
  int *tag_i;
  int num_tags, i;

  regoff_t match_eo = -1;	   /* end offset of match (-1 if no match found yet) */
  int new_match = 0;
  regoff_t *tmp_tags = NULL;
  regoff_t *tmp_iptr;

#ifdef TRE_MBSTATE
  memset(&mbstate, '\0', sizeof(mbstate));
#endif /* TRE_MBSTATE */

  if (!match_tags)
    num_tags = 0;
  else
    num_tags = tnfa->num_tags;

  /* Allocate memory for temporary data required for matching.	This needs to
     be done for every matching operation to be thread safe.  This allocates
     everything in a single large block with calloc(). */
  {
    size_t tbytes, rbytes, pbytes, xbytes, total_bytes;
    char *tmp_buf;

    /* Ensure that tbytes and xbytes*num_states cannot overflow, and that
     * they don't contribute more than 1/8 of SIZE_MAX to total_bytes. */
    if ((size_t)num_tags > SIZE_MAX/(8 * sizeof(regoff_t) * tnfa->num_states))
      return REG_ESPACE;

    /* Likewise check rbytes. */
    if ((size_t)tnfa->num_states+1 > SIZE_MAX/(8 * sizeof(*reach_next)))
      return REG_ESPACE;

    /* Likewise check pbytes. */
    if ((size_t)tnfa->num_states > SIZE_MAX/(8 * sizeof(*reach_pos)))
      return REG_ESPACE;

    /* Compute the length of the block we need. */
    tbytes = sizeof(*tmp_tags) * num_tags;
    rbytes = sizeof(*reach_next) * (tnfa->num_states + 1);
    pbytes = sizeof(*reach_pos) * tnfa->num_states;
    xbytes = sizeof(regoff_t) * num_tags;
    total_bytes =
      (sizeof(long) - 1) * 4 /* for alignment paddings */
      + (rbytes + xbytes * tnfa->num_states) * 2 + tbytes + pbytes;

    /* Allocate the memory. */
    buf = calloc(total_bytes, 1);
    if (buf == NULL)
      return REG_ESPACE;

    /* Get the various pointers within tmp_buf (properly aligned). */
    tmp_tags = (void *)buf;
    tmp_buf = buf + tbytes;
    tmp_buf += ALIGN(tmp_buf, long);
    reach_next = (void *)tmp_buf;
    tmp_buf += rbytes;
    tmp_buf += ALIGN(tmp_buf, long);
    reach = (void *)tmp_buf;
    tmp_buf += rbytes;
    tmp_buf += ALIGN(tmp_buf, long);
    reach_pos = (void *)tmp_buf;
    tmp_buf += pbytes;
    tmp_buf += ALIGN(tmp_buf, long);
    for (i = 0; i < tnfa->num_states; i++)
      {
	reach[i].tags = (void *)tmp_buf;
	tmp_buf += xbytes;
	reach_next[i].tags = (void *)tmp_buf;
	tmp_buf += xbytes;
      }
  }

  for (i = 0; i < tnfa->num_states; i++)
    reach_pos[i].pos = -1;

  GET_NEXT_WCHAR();
  pos = 0;

  reach_next_i = reach_next;
  while (1)
    {
      /* If no match found yet, add the initial states to `reach_next'. */
      if (match_eo < 0)
	{
	  trans_i = tnfa->initial;
	  while (trans_i->state != NULL)
	    {
	      if (reach_pos[trans_i->state_id].pos < pos)
		{
		  if (trans_i->assertions
		      && CHECK_ASSERTIONS(trans_i->assertions))
		    {
		      trans_i++;
		      continue;
		    }

		  reach_next_i->state = trans_i->state;
		  for (i = 0; i < num_tags; i++)
		    reach_next_i->tags[i] = -1;
		  tag_i = trans_i->tags;
		  if (tag_i)
		    while (*tag_i >= 0)
		      {
			if (*tag_i < num_tags)
			  reach_next_i->tags[*tag_i] = pos;
			tag_i++;
		      }
		  if (reach_next_i->state == tnfa->final)
		    {
		      match_eo = pos;
		      new_match = 1;
		      for (i = 0; i < num_tags; i++)
			match_tags[i] = reach_next_i->tags[i];
		    }
		  reach_pos[trans_i->state_id].pos = pos;
		  reach_pos[trans_i->state_id].tags = &reach_next_i->tags;
		  reach_next_i++;
		}
	      trans_i++;
	    }
	  reach_next_i->state = NULL;
	}
      else
	{
	  if (num_tags == 0 || reach_next_i == reach_next)
	    /* We have found a match. */
	    break;
	}

      /* Check for end of string. */
      if (!next_c) break;

      GET_NEXT_WCHAR();

      /* Swap `reach' and `reach_next'. */
      reach_i = reach;
      reach = reach_next;
      reach_next = reach_i;

      /* For each state in `reach', weed out states that don't fulfill the
	 minimal matching conditions. */
      if (tnfa->num_minimals && new_match)
	{
	  new_match = 0;
	  reach_next_i = reach_next;
	  for (reach_i = reach; reach_i->state; reach_i++)
	    {
	      int skip = 0;
	      for (i = 0; tnfa->minimal_tags[i] >= 0; i += 2)
		{
		  int end = tnfa->minimal_tags[i];
		  int start = tnfa->minimal_tags[i + 1];
		  if (end >= num_tags)
		    {
		      skip = 1;
		      break;
		    }
		  else if (reach_i->tags[start] == match_tags[start]
			   && reach_i->tags[end] < match_tags[end])
		    {
		      skip = 1;
		      break;
		    }
		}
	      if (!skip)
		{
		  reach_next_i->state = reach_i->state;
		  tmp_iptr = reach_next_i->tags;
		  reach_next_i->tags = reach_i->tags;
		  reach_i->tags = tmp_iptr;
		  reach_next_i++;
		}
	    }
	  reach_next_i->state = NULL;

	  /* Swap `reach' and `reach_next'. */
	  reach_i = reach;
	  reach = reach_next;
	  reach_next = reach_i;
	}

      /* For each state in `reach' see if there is a transition leaving with
	 the current input symbol to a state not yet in `reach_next', and
	 add the destination states to `reach_next'. */
      reach_next_i = reach_next;
      for (reach_i = reach; reach_i->state; reach_i++)
	{
	  for (trans_i = reach_i->state; trans_i->state; trans_i++)
	    {
	      /* Does this transition match the input symbol? */
	      if (trans_i->code_min <= (tre_cint_t)prev_c &&
		  trans_i->code_max >= (tre_cint_t)prev_c)
		{
		  if (trans_i->assertions
		      && (CHECK_ASSERTIONS(trans_i->assertions)
			  || CHECK_CHAR_CLASSES(trans_i, tnfa, eflags)))
		    {
		      continue;
		    }

		  /* Compute the tags after this transition. */
		  for (i = 0; i < num_tags; i++)
		    tmp_tags[i] = reach_i->tags[i];
		  tag_i = trans_i->tags;
		  if (tag_i != NULL)
		    while (*tag_i >= 0)
		      {
			if (*tag_i < num_tags)
			  tmp_tags[*tag_i] = pos;
			tag_i++;
		      }

		  if (reach_pos[trans_i->state_id].pos < pos)
		    {
		      /* Found an unvisited node. */
		      reach_next_i->state = trans_i->state;
		      tmp_iptr = reach_next_i->tags;
		      reach_next_i->tags = tmp_tags;
		      tmp_tags = tmp_iptr;
		      reach_pos[trans_i->state_id].pos = pos;
		      reach_pos[trans_i->state_id].tags = &reach_next_i->tags;

		      if (reach_next_i->state == tnfa->final
			  && (match_eo == -1
			      || (num_tags > 0
				  && reach_next_i->tags[0] <= match_tags[0])))
			{
			  match_eo = pos;
			  new_match = 1;
			  for (i = 0; i < num_tags; i++)
			    match_tags[i] = reach_next_i->tags[i];
			}
		      reach_next_i++;

		    }
		  else
		    {
		      assert(reach_pos[trans_i->state_id].pos == pos);
		      /* Another path has also reached this state.  We choose
			 the winner by examining the tag values for both
			 paths. */
		      if (tre_tag_order(num_tags, tnfa->tag_directions,
					tmp_tags,
					*reach_pos[trans_i->state_id].tags))
			{
			  /* The new path wins. */
			  tmp_iptr = *reach_pos[trans_i->state_id].tags;
			  *reach_pos[trans_i->state_id].tags = tmp_tags;
			  if (trans_i->state == tnfa->final)
			    {
			      match_eo = pos;
			      new_match = 1;
			      for (i = 0; i < num_tags; i++)
				match_tags[i] = tmp_tags[i];
			    }
			  tmp_tags = tmp_iptr;
			}
		    }
		}
	    }
	}
      reach_next_i->state = NULL;
    }

  *match_end_ofs = match_eo;
  ret = match_eo >= 0 ? REG_OK : REG_NOMATCH;
error_exit:
  xfree(buf);
  return ret;
}

int regcomp(regex_t *restrict preg, const char *restrict regex, int cflags)
{
  tre_stack_t *stack;
  tre_ast_node_t *tree, *tmp_ast_l, *tmp_ast_r;
  tre_pos_and_tags_t *p;
  int *counts = NULL, *offs = NULL;
  int i, add = 0;
  tre_tnfa_transition_t *transitions, *initial;
  tre_tnfa_t *tnfa = NULL;
  tre_submatch_data_t *submatch_data;
  tre_tag_direction_t *tag_directions = NULL;
  reg_errcode_t errcode;
  tre_mem_t mem;

  /* Parse context. */
  tre_parse_ctx_t parse_ctx;

  /* Allocate a stack used throughout the compilation process for various
     purposes. */
  stack = tre_stack_new(512, 1024000, 128);
  if (!stack)
    return REG_ESPACE;
  /* Allocate a fast memory allocator. */
  mem = tre_mem_new();
  if (!mem)
    {
      tre_stack_destroy(stack);
      return REG_ESPACE;
    }

  /* Parse the regexp. */
  memset(&parse_ctx, 0, sizeof(parse_ctx));
  parse_ctx.mem = mem;
  parse_ctx.stack = stack;
  parse_ctx.start = regex;
  parse_ctx.cflags = cflags;
  parse_ctx.max_backref = -1;
  errcode = tre_parse(&parse_ctx);
  if (errcode != REG_OK)
    ERROR_EXIT(errcode);
  preg->re_nsub = parse_ctx.submatch_id - 1;
  tree = parse_ctx.n;

#ifdef TRE_DEBUG
  tre_ast_print(tree);
#endif /* TRE_DEBUG */

  /* Referring to nonexistent subexpressions is illegal. */
  if (parse_ctx.max_backref > (int)preg->re_nsub)
    ERROR_EXIT(REG_ESUBREG);

  /* Allocate the TNFA struct. */
  tnfa = xcalloc(1, sizeof(tre_tnfa_t));
  if (tnfa == NULL)
    ERROR_EXIT(REG_ESPACE);
  tnfa->have_backrefs = parse_ctx.max_backref >= 0;
  tnfa->have_approx = 0;
  tnfa->num_submatches = parse_ctx.submatch_id;

  /* Set up tags for submatch addressing.  If REG_NOSUB is set and the
     regexp does not have back references, this can be skipped. */
  if (tnfa->have_backrefs || !(cflags & REG_NOSUB))
    {

      /* Figure out how many tags we will need. */
      errcode = tre_add_tags(NULL, stack, tree, tnfa);
      if (errcode != REG_OK)
	ERROR_EXIT(errcode);

      if (tnfa->num_tags > 0)
	{
	  tag_directions = xmalloc(sizeof(*tag_directions)
				   * (tnfa->num_tags + 1));
	  if (tag_directions == NULL)
	    ERROR_EXIT(REG_ESPACE);
	  tnfa->tag_directions = tag_directions;
	  memset(tag_directions, -1,
		 sizeof(*tag_directions) * (tnfa->num_tags + 1));
	}
      tnfa->minimal_tags = xcalloc((unsigned)tnfa->num_tags * 2 + 1,
				   sizeof(*tnfa->minimal_tags));
      if (tnfa->minimal_tags == NULL)
	ERROR_EXIT(REG_ESPACE);

      submatch_data = xcalloc((unsigned)parse_ctx.submatch_id,
			      sizeof(*submatch_data));
      if (submatch_data == NULL)
	ERROR_EXIT(REG_ESPACE);
      tnfa->submatch_data = submatch_data;

      errcode = tre_add_tags(mem, stack, tree, tnfa);
      if (errcode != REG_OK)
	ERROR_EXIT(errcode);

    }

  /* Expand iteration nodes. */
  errcode = tre_expand_ast(mem, stack, tree, &parse_ctx.position,
			   tag_directions);
  if (errcode != REG_OK)
    ERROR_EXIT(errcode);

  /* Add a dummy node for the final state.
     XXX - For certain patterns this dummy node can be optimized away,
	   for example "a*" or "ab*".	Figure out a simple way to detect
	   this possibility. */
  tmp_ast_l = tree;
  tmp_ast_r = tre_ast_new_literal(mem, 0, 0, parse_ctx.position++);
  if (tmp_ast_r == NULL)
    ERROR_EXIT(REG_ESPACE);

  tree = tre_ast_new_catenation(mem, tmp_ast_l, tmp_ast_r);
  if (tree == NULL)
    ERROR_EXIT(REG_ESPACE);

  errcode = tre_compute_nfl(mem, stack, tree);
  if (errcode != REG_OK)
    ERROR_EXIT(errcode);

  counts = xmalloc(sizeof(int) * parse_ctx.position);
  if (counts == NULL)
    ERROR_EXIT(REG_ESPACE);

  offs = xmalloc(sizeof(int) * parse_ctx.position);
  if (offs == NULL)
    ERROR_EXIT(REG_ESPACE);

  for (i = 0; i < parse_ctx.position; i++)
    counts[i] = 0;
  tre_ast_to_tnfa(tree, NULL, counts, NULL);

  add = 0;
  for (i = 0; i < parse_ctx.position; i++)
    {
      offs[i] = add;
      add += counts[i] + 1;
      counts[i] = 0;
    }
  transitions = xcalloc((unsigned)add + 1, sizeof(*transitions));
  if (transitions == NULL)
    ERROR_EXIT(REG_ESPACE);
  tnfa->transitions = transitions;
  tnfa->num_transitions = add;

  errcode = tre_ast_to_tnfa(tree, transitions, counts, offs);
  if (errcode != REG_OK)
    ERROR_EXIT(errcode);

  tnfa->firstpos_chars = NULL;

  p = tree->firstpos;
  i = 0;
  while (p->position >= 0)
    {
      i++;
      p++;
    }

  initial = xcalloc((unsigned)i + 1, sizeof(tre_tnfa_transition_t));
  if (initial == NULL)
    ERROR_EXIT(REG_ESPACE);
  tnfa->initial = initial;

  i = 0;
  for (p = tree->firstpos; p->position >= 0; p++)
    {
      initial[i].state = transitions + offs[p->position];
      initial[i].state_id = p->position;
      initial[i].tags = NULL;
      /* Copy the arrays p->tags, and p->params, they are allocated
	 from a tre_mem object. */
      if (p->tags)
	{
	  int j;
	  for (j = 0; p->tags[j] >= 0; j++);
	  initial[i].tags = xmalloc(sizeof(*p->tags) * (j + 1));
	  if (!initial[i].tags)
	    ERROR_EXIT(REG_ESPACE);
	  memcpy(initial[i].tags, p->tags, sizeof(*p->tags) * (j + 1));
	}
      initial[i].assertions = p->assertions;
      i++;
    }
  initial[i].state = NULL;

  tnfa->num_transitions = add;
  tnfa->final = transitions + offs[tree->lastpos[0].position];
  tnfa->num_states = parse_ctx.position;
  tnfa->cflags = cflags;

  tre_mem_destroy(mem);
  tre_stack_destroy(stack);
  xfree(counts);
  xfree(offs);

  preg->TRE_REGEX_T_FIELD = (void *)tnfa;
  return REG_OK;

 error_exit:
  /* Free everything that was allocated and return the error code. */
  tre_mem_destroy(mem);
  if (stack != NULL)
    tre_stack_destroy(stack);
  if (counts != NULL)
    xfree(counts);
  if (offs != NULL)
    xfree(offs);
  preg->TRE_REGEX_T_FIELD = (void *)tnfa;
  regfree(preg);
  return errcode;
}

int
regexec(const regex_t *restrict preg, const char *restrict string,
	  size_t nmatch, regmatch_t * restrict pmatch, int eflags)
{
  tre_tnfa_t *tnfa = (void *)preg->TRE_REGEX_T_FIELD;
  reg_errcode_t status;
  regoff_t *tags = NULL, eo;
  if (tnfa->cflags & REG_NOSUB) nmatch = 0;
  if (tnfa->num_tags > 0 && nmatch > 0)
    {
      tags = xmalloc(sizeof(*tags) * tnfa->num_tags);
      if (tags == NULL)
	return REG_ESPACE;
    }

  /* Dispatch to the appropriate matcher. */
  if (tnfa->have_backrefs)
    {
      /* The regex has back references, use the backtracking matcher. */
      status = tre_tnfa_run_backtrack(tnfa, string, tags, eflags, &eo);
    }
  else
    {
      /* Exact matching, no back references, use the parallel matcher. */
      status = tre_tnfa_run_parallel(tnfa, string, tags, eflags, &eo);
    }

  if (status == REG_OK)
    /* A match was found, so fill the submatch registers. */
    tre_fill_pmatch(nmatch, pmatch, tnfa->cflags, tnfa, tags, eo);
  if (tags)
    xfree(tags);
  return status;
}

void
regfree(regex_t *preg)
{
  tre_tnfa_t *tnfa;
  unsigned int i;
  tre_tnfa_transition_t *trans;

  tnfa = (void *)preg->TRE_REGEX_T_FIELD;
  if (!tnfa)
    return;

  for (i = 0; i < tnfa->num_transitions; i++)
    if (tnfa->transitions[i].state)
      {
	if (tnfa->transitions[i].tags)
	  xfree(tnfa->transitions[i].tags);
	if (tnfa->transitions[i].neg_classes)
	  xfree(tnfa->transitions[i].neg_classes);
      }
  if (tnfa->transitions)
    xfree(tnfa->transitions);

  if (tnfa->initial)
    {
      for (trans = tnfa->initial; trans->state; trans++)
	{
	  if (trans->tags)
	    xfree(trans->tags);
	}
      xfree(tnfa->initial);
    }

  if (tnfa->submatch_data)
    {
      for (i = 0; i < tnfa->num_submatches; i++)
	if (tnfa->submatch_data[i].parents)
	  xfree(tnfa->submatch_data[i].parents);
      xfree(tnfa->submatch_data);
    }

  if (tnfa->tag_directions)
    xfree(tnfa->tag_directions);
  if (tnfa->firstpos_chars)
    xfree(tnfa->firstpos_chars);
  if (tnfa->minimal_tags)
    xfree(tnfa->minimal_tags);
  xfree(tnfa);
}

size_t regerror(int e, const regex_t *restrict preg, char *restrict buf, size_t size)
{
  (void)preg;
	const char *s;
	for (s=messages; e && *s; e--, s+=strlen(s)+1);
	if (!*s) s++;
	return 1+snprintf(buf, size, "%s", s);
}
