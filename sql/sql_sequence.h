/* Copyright (c) 2017, MariaDB corporation

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; version 2 of the License.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
*/

#ifndef SQL_SEQUENCE_INCLUDED
#define SQL_SEQUENCE_INCLUDED

#define seq_field_used_min_value 1
#define seq_field_used_max_value 2
#define seq_field_used_start     4
#define seq_field_used_increment 8
#define seq_field_used_cache     16
#define seq_field_used_cycle     32
#define seq_field_used_restart   64
#define seq_field_used_restart_value 128
#define seq_field_used_as 256
#define seq_field_specified_min_value 512
#define seq_field_specified_max_value 1024

/* Field position in sequence table for some fields we refer to directly */
#define NEXT_FIELD_NO 0
#define MIN_VALUE_FIELD_NO 1
#define ROUND_FIELD_NO 7

#include "mysql_com.h"
#include "sql_type_int.h"

class Create_field;
class Type_handler;

struct Sequence_field_definition
{
  const char *field_name;
  uint length;
  const Type_handler *type_handler;
  LEX_CSTRING comment;
  ulong flags;
};

struct Sequence_row_definition
{
  Sequence_field_definition fields[9];
};

/**
   sequence_definition is used when defining a sequence as part of create
*/

class sequence_definition :public Sql_alloc
{
public:
  sequence_definition():
    reserved_until(0, false), min_value(1, false), max_value(LONGLONG_MAX-1, false), start(1, false),
    increment(1), cache(1000), round(0), restart(0, false), cycle(0),
    used_fields(0), value_type(MYSQL_TYPE_LONGLONG),
    next_free_value(0, false)
  {}
  Longlong_hybrid reserved_until;
  Longlong_hybrid min_value;
  Longlong_hybrid max_value;
  Longlong_hybrid start;
  longlong increment;
  longlong cache;                // cache size
  ulonglong round;
  Longlong_hybrid restart;       // alter sequence restart value
  bool     cycle;
  uint used_fields;              // Which fields where used or specified in CREATE
  enum enum_field_types value_type;    // value type of the sequence

  Type_handler const *value_type_handler();
  // Whether the sequence is unsigned.
  bool is_unsigned() const;
  // max value for the value type, e.g. 32767 for smallint.
  longlong value_type_max();
  // min value for the value type, e.g. -32768 for smallint.
  longlong value_type_min();
  bool check_and_adjust(THD *thd, bool set_reserved_until);
  void store_fields(TABLE *table);
  void read_fields(TABLE *table);
  int write_initial_sequence(TABLE *table);
  int write(TABLE *table, bool all_fields);
  /* This must be called after sequence data has been updated */
  void adjust_values(Longlong_hybrid next_value);
  inline void print_dbug()
  {
    DBUG_PRINT("sequence", ("reserved: %lld  start: %lld  increment: %lld  min_value: %lld  max_value: %lld  cache: %lld  round: %lld",
                      reserved_until, start, increment, min_value,
                        max_value, cache, round));
  }
  static bool is_allowed_value_type(enum_field_types type);
  bool prepare_sequence_fields(List<Create_field> *fields);
  
protected:
  /*
    The following values are the values from sequence_definition
    merged with global auto_increment_offset and auto_increment_increment
  */
  longlong real_increment;
  Longlong_hybrid next_free_value;
};

/**
  SEQUENCE is in charge of managing the sequence values.
  It's also responsible to generate new values and updating the sequence
  table (engine=SQL_SEQUENCE) trough it's specialized handler interface.

  If increment is 0 then the sequence will be be using
  auto_increment_increment and auto_increment_offset variables, just like
  AUTO_INCREMENT is using.
*/

class SEQUENCE :public sequence_definition
{
public:
  enum seq_init { SEQ_UNINTIALIZED, SEQ_IN_PREPARE, SEQ_IN_ALTER,
                  SEQ_READY_TO_USE };
  SEQUENCE();
  ~SEQUENCE();
  int  read_initial_values(TABLE *table);
  int  read_stored_values(TABLE *table);
  void write_lock(TABLE *table);
  void write_unlock(TABLE *table);
  void read_lock(TABLE *table);
  void read_unlock(TABLE *table);
  void copy(sequence_definition *seq)
  {
    sequence_definition::operator= (*seq);
    adjust_values(reserved_until);
    all_values_used= 0;
  }
  longlong next_value(TABLE *table, bool second_round, int *error);
  int set_value(TABLE *table, longlong next_value, ulonglong round_arg,
                bool is_used);
  longlong increment_value(longlong value)
  {
    if (real_increment > 0)
    {
      // here we have an issue where there's no subtraction operator
      // for Longlong_hybrid. How do we design subtraction operator?
      // what if the two operands are of different signedness? That's
      // a further design issue.
      if (value > max_value - Longlong_hybrid(real_increment, is_unsigned()) ||
          value + Longlong_hybrid(real_increment, is_unsigned()) > max_value)
        value= max_value + 1;
      else
        value+= Longlong_hybrid(real_increment, is_unsigned());
    }
    else
    {
      if (value + real_increment < min_value ||
          value < min_value - real_increment)
        value= min_value - 1;
      else
        value+= real_increment;
    }
    return value;
  }

  bool all_values_used;
  seq_init initialized;

private:
  mysql_rwlock_t mutex;
};


/**
  Class to cache last value of NEXT VALUE from the sequence
*/

class SEQUENCE_LAST_VALUE
{
public:
  SEQUENCE_LAST_VALUE(uchar *key_arg, uint length_arg)
    :key(key_arg), length(length_arg), value(0, false)
  {}
  ~SEQUENCE_LAST_VALUE()
  { my_free((void*) key); }
  /* Returns 1 if table hasn't been dropped or re-created */
  bool check_version(TABLE *table);
  void set_version(TABLE *table);

  const uchar *key;
  uint length;
  bool null_value;
  Longlong_hybrid value;
  uchar table_version[MY_UUID_SIZE];
};

extern bool check_sequence_fields(LEX *lex, List<Create_field> *fields);
extern bool sequence_insert(THD *thd, LEX *lex, TABLE_LIST *table_list);
#endif /* SQL_SEQUENCE_INCLUDED */
