/****************************************************************************
 * apps/system/perf-tools/builtin-stat.c
 *
 * Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.  The
 * ASF licenses this file to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance with the
 * License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
 * License for the specific language governing permissions and limitations
 * under the License.
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <time.h>
#include <sys/ioctl.h>
#include <sys/param.h>

#include <nuttx/perf.h>
#include <nuttx/list.h>

#include "builtin.h"
#include "evlist.h"
#include "evsel.h"
#include "parse-options.h"

/****************************************************************************
 * Private Definitions
 ****************************************************************************/

#define PERF_STAT_DEFAULT_RUN_TIME    5
#define PERF_STAT_DEFAULT_MAX_EVSTR   64
#define PERF_STAT_DEFAULT_MAX_CMDSTR  128

/****************************************************************************
 * Private Data
 ****************************************************************************/

static const char stat_usage[] =
  "perf stat [<options>] [<command>]";

static const struct option_s stat_options[] =
{
  OPT_STRING('a', "all-cpus", "system-wide collection from all CPUs"),
  OPT_STRING('C', "cpu <cpu>", "list of cpus to monitor in system-wide"),
  OPT_STRING('e', "event <event>",
             "event selector. use 'perf list' to list available events"),
  OPT_STRING('p', "pid <pid>", "stat events on existing process id"),
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static void stat_cmds_help(int sname)
{
  unsigned int i = 0;
  unsigned int longest = 0;

  printf("\n Usage: %s\n\n", stat_usage);

  for (i = 0; i < nitems(stat_options); i++)
    {
      if (longest < strlen(stat_options[i].long_name))
        {
          longest = strlen(stat_options[i].long_name);
        }
    }

  for (i = 0; i < nitems(stat_options); i++)
    {
      if (!sname || sname == stat_options[i].short_name)
        {
          printf("   -%c, --%-*s   %s\n", stat_options[i].short_name,
                                  longest, stat_options[i].long_name,
                                  stat_options[i].help);
        }
    }

  printf("\n");
}

static void print_counters(FAR struct evlist_s *evlist,
                           FAR struct timespec *ts)
{
  printf("\n Performance counter stats\n\n");

  evlist_print_counters(evlist);

  printf("\n %ld.%09ld seconds time elapsed\n", ts->tv_sec, ts->tv_nsec);
}

static int check_event(FAR const char *evstr)
{
  char buf[PERF_STAT_DEFAULT_MAX_EVSTR];
  int evnum = 0;
  int cnum = 0;
  int i;
  int j;

  for (i = 0; i <= strlen(evstr); i++)
    {
      if (evstr[i] == ',' || evstr[i] == '\0')
        {
          memset(buf, 0, sizeof(buf));
          if (i - cnum > sizeof(buf))
            {
              printf("The event name is too long, %d\n", i - cnum);
              return 0;
            }

          strncpy(buf, evstr + cnum, i - cnum);
          for (j = 0; j < PERF_COUNT_HW_MAX; j++)
            {
              if (!strcmp(evsel_hw_names[j], buf))
                {
                  break;
                }
            }

          if (j >= PERF_COUNT_HW_MAX)
            {
              return 0;
            }

          cnum = i + 1;
          evnum++;
        }
    }

  return evnum;
}

static int set_specified_attributes(FAR const char *evstr,
                      FAR struct perf_event_attr_s *attrs)
{
  char buf[PERF_STAT_DEFAULT_MAX_EVSTR];
  int evnum = 0;
  int cnum = 0;
  int i;
  int j;

  for (i = 0; i <= strlen(evstr); i++)
    {
      if (evstr[i] == ',' || evstr[i] == '\0')
        {
          memset(buf, 0, sizeof(buf));
          if (i - cnum > sizeof(buf))
            {
              printf("The event name is too long, %d\n", i - cnum);
              return -EINVAL;
            }

          strncpy(buf, evstr + cnum, i - cnum);
          for (j = 0; j < PERF_COUNT_HW_MAX; j++)
            {
              if (!strcmp(evsel_hw_names[j], buf))
                {
                  attrs[evnum].type = PERF_TYPE_HARDWARE;
                  attrs[evnum].config = j;
                  attrs[evnum].size = sizeof(struct perf_event_attr_s);
                  attrs[evnum].disabled = 1;
                  attrs[evnum].inherit = 1;
                  break;
                }
            }

          cnum = i + 1;
          evnum++;
        }
    }

  return 0;
}

static int add_specified_attributes(FAR struct evlist_s *evlist,
                                    FAR const char *evstr)
{
  int status;
  int evnum = 0;

  if (!evstr)
    {
      return -EINVAL;
    }

  evnum = check_event(evstr);
  if (!evnum)
    {
      printf("Event syntax error: %s\n", evstr);
      return -EINVAL;
    }

  evlist->attrs = zalloc(evnum * sizeof(struct perf_event_attr_s));
  if (!evlist->attrs)
    {
      printf("Zalloc attrs failed!\n");
      return -ENOMEM;
    }

  set_specified_attributes(evstr, evlist->attrs);

  if (evlist->system_wide)
    {
      for (int i = 0; i < CONFIG_SMP_NCPUS; i++)
        {
          status = evlist_add_attrs(evlist, evlist->attrs, evnum, i);
        }
    }
  else
    {
      status = evlist_add_attrs(evlist, evlist->attrs, evnum, evlist->cpu);
    }

  return status;
}

static int set_default_attributes(FAR int *config, int nr,
                                  FAR struct perf_event_attr_s *attrs)
{
  int i;

  for (i = 0; i < nr; i++)
    {
      attrs[i].type = PERF_TYPE_HARDWARE;
      attrs[i].config = config[i];
      attrs[i].size = sizeof(struct perf_event_attr_s);
      attrs[i].disabled = 1;
      attrs[i].inherit = 1;
    }

  return 0;
}

static int add_default_attributes(FAR struct evlist_s *evlist)
{
  int status = 0;
  int evnum = nitems(default_hw_config);

  evlist->attrs = zalloc(evnum * sizeof(struct perf_event_attr_s));
  if (!evlist->attrs)
    {
      printf("Zalloc attrs failed!\n");
      return -ENOMEM;
    }

  set_default_attributes(default_hw_config, evnum,  evlist->attrs);

  if (evlist->system_wide)
    {
      for (int i = 0; i < CONFIG_SMP_NCPUS; i++)
        {
          status = evlist_add_attrs(evlist, evlist->attrs, evnum, i);
        }
    }
  else
    {
      status = evlist_add_attrs(evlist, evlist->attrs, evnum, evlist->cpu);
    }

  return status;
}

int perf_stat_handle(FAR struct evlist_s *evlist)
{
  int status = 0;

  evlist_for_each_evsel(evlist, node, evsel_count_start, status);

  if (evlist->cmd_str)
    {
      status = system(evlist->cmd_str);
      if (status)
        {
          return status;
        }
    }
  else
    {
      evlist->sec = PERF_STAT_DEFAULT_RUN_TIME;
      sleep(PERF_STAT_DEFAULT_RUN_TIME);
    }

  evlist_for_each_evsel(evlist, node, evsel_read_counter, status);

  evlist_for_each_evsel(evlist, node, evsel_count_end, status);

  return status;
}

static int run_perf_stat(FAR struct evlist_s *evlist,
                         FAR struct stat_args_s *stat_args)
{
  int status = 0;
  int i;
  char cmd_str[PERF_STAT_DEFAULT_MAX_CMDSTR];
  struct timespec start_time;
  struct timespec end_time;
  struct timespec run_time;

  memset(cmd_str, '\0', sizeof(cmd_str));

  evlist->cpu = stat_args->cpu;
  evlist->pid = stat_args->pid;

  if (stat_args->cmd_nr > 0)
    {
      for (i = 0; i < stat_args->cmd_nr; i++)
        {
          strcat(cmd_str, stat_args->cmd_args[i]);
          strcat(cmd_str, " ");
        }

      evlist->cmd_str = cmd_str;
    }

  if (stat_args->events)
    {
      evlist->defult_attrs = false;
      status = add_specified_attributes(evlist, stat_args->events);
      if (status)
        {
          stat_cmds_help(0);
          return status;
        }
    }
  else
    {
      evlist->defult_attrs = true;
      status = add_default_attributes(evlist);
      if (status)
        {
          return status;
        }
    }

  clock_gettime(CLOCK_MONOTONIC, &start_time);

  status = perf_stat_handle(evlist);
  if (status)
    {
      return status;
    }

  clock_gettime(CLOCK_MONOTONIC, &end_time);

  run_time.tv_sec = end_time.tv_sec - start_time.tv_sec;
  run_time.tv_nsec = end_time.tv_nsec - start_time.tv_nsec;

  print_counters(evlist, &run_time);

  return status;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int cmd_stat(int argc, FAR const char **argv)
{
  FAR static struct evlist_s *evlist;
  int status = 0;
  struct stat_args_s stat_args;

  memset(&stat_args, 0, sizeof(struct stat_args_s));

  status = parse_stat_options(argc, (char **)argv, &stat_args);
  if (status || stat_args.type == STAT_ARGS_HELP)
    {
      stat_cmds_help(0);
      return status;
    }

  evlist = evlist_new();
  if (evlist == NULL)
    {
      return -ENOMEM;
    }

  evlist->type = stat_args.type;
  if (stat_args.pid == -1 && stat_args.cpu == -1)
    {
      evlist->system_wide = true;
    }
  else
    {
      evlist->system_wide = false;
    }

  status = run_perf_stat(evlist, &stat_args);

  evlist_delete(evlist);

  return status;
}