/* get the concurrency level
 */

static size_t kaapi_conf_get_cpucount(void)
{
  static size_t cpu_count = 0;

  if (cpu_count == 0)
  {
    const const char* s = getenv("KAAPI_CPUCOUNT");
    cpu_count = sysconf(_SC_NPROCESSORS_CONF);
    /* environ overrides physical count */
    if (s != NULL) cpu_count = atoi(s);
  }

  return cpu_count;
}
