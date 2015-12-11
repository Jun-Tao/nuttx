/****************************************************************************
 * binfmt/module.c
 *
 *   Copyright (C) 2015 Gregory Nutt. All rights reserved.
 *   Author: Gregory Nutt <gnutt@nuttx.org>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name NuttX nor the names of its contributors may be
 *    used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
 * OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <sys/types.h>
#include <stdint.h>
#include <string.h>
#include <elf32.h>
#include <debug.h>
#include <errno.h>

#include <arpa/inet.h>

#include <nuttx/arch.h>
#include <nuttx/binfmt/binfmt.h>
#include <nuttx/binfmt/module.h>

#include "libmodule/libmodule.h"

#ifdef CONFIG_MODULE

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* CONFIG_DEBUG, CONFIG_DEBUG_VERBOSE, and CONFIG_DEBUG_BINFMT have to be
 * defined or CONFIG_MODULE_DUMPBUFFER does nothing.
 */

#if !defined(CONFIG_DEBUG_VERBOSE) || !defined (CONFIG_DEBUG_BINFMT)
#  undef CONFIG_MODULE_DUMPBUFFER
#endif

#ifdef CONFIG_MODULE_DUMPBUFFER
# define mod_dumpbuffer(m,b,n) bvdbgdumpbuffer(m,b,n)
#else
# define mod_dumpbuffer(m,b,n)
#endif

#ifndef MIN
#  define MIN(a,b) (a < b ? a : b)
#endif

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static int mod_loadbinary(FAR struct binary_s *binp);
#if defined(CONFIG_DEBUG) && defined(CONFIG_DEBUG_BINFMT)
static void mod_dumploadinfo(FAR struct mod_loadinfo_s *loadinfo);
#endif

/****************************************************************************
 * Private Data
 ****************************************************************************/

static struct binfmt_s g_modbinfmt =
{
  NULL,             /* next */
  mod_loadbinary,   /* load */
  NULL,             /* unload */
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: mod_dumploadinfo
 ****************************************************************************/

#if defined(CONFIG_DEBUG) && defined(CONFIG_DEBUG_BINFMT)
static void mod_dumploadinfo(FAR struct mod_loadinfo_s *loadinfo)
{
  int i;

  bdbg("LOAD_INFO:\n");
  bdbg("  textalloc:    %08lx\n", (long)loadinfo->textalloc);
  bdbg("  dataalloc:    %08lx\n", (long)loadinfo->dataalloc);
  bdbg("  textsize:     %ld\n",   (long)loadinfo->textsize);
  bdbg("  datasize:     %ld\n",   (long)loadinfo->datasize);
  bdbg("  filelen:      %ld\n",   (long)loadinfo->filelen);
#ifdef CONFIG_BINFMT_CONSTRUCTORS
  bdbg("  ctoralloc:    %08lx\n", (long)loadinfo->ctoralloc);
  bdbg("  ctors:        %08lx\n", (long)loadinfo->ctors);
  bdbg("  nctors:       %d\n",    loadinfo->nctors);
  bdbg("  dtoralloc:    %08lx\n", (long)loadinfo->dtoralloc);
  bdbg("  dtors:        %08lx\n", (long)loadinfo->dtors);
  bdbg("  ndtors:       %d\n",    loadinfo->ndtors);
#endif
  bdbg("  filfd:        %d\n",    loadinfo->filfd);
  bdbg("  symtabidx:    %d\n",    loadinfo->symtabidx);
  bdbg("  strtabidx:    %d\n",    loadinfo->strtabidx);

  bdbg("ELF Header:\n");
  bdbg("  e_ident:      %02x %02x %02x %02x\n",
    loadinfo->ehdr.e_ident[0], loadinfo->ehdr.e_ident[1],
    loadinfo->ehdr.e_ident[2], loadinfo->ehdr.e_ident[3]);
  bdbg("  e_type:       %04x\n",  loadinfo->ehdr.e_type);
  bdbg("  e_machine:    %04x\n",  loadinfo->ehdr.e_machine);
  bdbg("  e_version:    %08x\n",  loadinfo->ehdr.e_version);
  bdbg("  e_entry:      %08lx\n", (long)loadinfo->ehdr.e_entry);
  bdbg("  e_phoff:      %d\n",    loadinfo->ehdr.e_phoff);
  bdbg("  e_shoff:      %d\n",    loadinfo->ehdr.e_shoff);
  bdbg("  e_flags:      %08x\n" , loadinfo->ehdr.e_flags);
  bdbg("  e_ehsize:     %d\n",    loadinfo->ehdr.e_ehsize);
  bdbg("  e_phentsize:  %d\n",    loadinfo->ehdr.e_phentsize);
  bdbg("  e_phnum:      %d\n",    loadinfo->ehdr.e_phnum);
  bdbg("  e_shentsize:  %d\n",    loadinfo->ehdr.e_shentsize);
  bdbg("  e_shnum:      %d\n",    loadinfo->ehdr.e_shnum);
  bdbg("  e_shstrndx:   %d\n",    loadinfo->ehdr.e_shstrndx);

  if (loadinfo->shdr && loadinfo->ehdr.e_shnum > 0)
    {
      for (i = 0; i < loadinfo->ehdr.e_shnum; i++)
        {
          FAR Elf32_Shdr *shdr = &loadinfo->shdr[i];
          bdbg("Sections %d:\n", i);
          bdbg("  sh_name:      %08x\n", shdr->sh_name);
          bdbg("  sh_type:      %08x\n", shdr->sh_type);
          bdbg("  sh_flags:     %08x\n", shdr->sh_flags);
          bdbg("  sh_addr:      %08x\n", shdr->sh_addr);
          bdbg("  sh_offset:    %d\n",   shdr->sh_offset);
          bdbg("  sh_size:      %d\n",   shdr->sh_size);
          bdbg("  sh_link:      %d\n",   shdr->sh_link);
          bdbg("  sh_info:      %d\n",   shdr->sh_info);
          bdbg("  sh_addralign: %d\n",   shdr->sh_addralign);
          bdbg("  sh_entsize:   %d\n",   shdr->sh_entsize);
        }
    }
}
#else
# define mod_dumploadinfo(i)
#endif

/****************************************************************************
 * Name: mod_dumpinitializer
 ****************************************************************************/

#ifdef CONFIG_MODULE_DUMPBUFFER
static void mod_dumpinitializer(mod_initializer_t initializer,
                                FAR struct mod_loadinfo_s *loadinfo)
{
  mod_dumpbuffer("Initializer code", (FAR const uint8_t *)initializer,
                 MIN(loadinfo->textsize - loadinfo->ehdr.e_entry, 512));
}
#else
# define mod_dumpinitializer(b,l)
#endif

/****************************************************************************
 * Name: mod_loadbinary
 *
 * Description:
 *   Verify that the file is an ELF binary and, if so, load the ELF
 *   binary into memory
 *
 ****************************************************************************/

static int mod_loadbinary(FAR struct binary_s *binp)
{
  struct mod_loadinfo_s loadinfo;  /* Contains globals for libmodule */
  mod_initializer_t     initializer;
  int                   ret;

  bvdbg("Loading file: %s\n", binp->filename);

  /* Initialize the ELF library to load the program binary. */

  ret = libmod_initialize(binp->filename, &loadinfo);
  mod_dumploadinfo(&loadinfo);
  if (ret != 0)
    {
      bdbg("Failed to initialize for load of ELF program: %d\n", ret);
      goto errout;
    }

  /* Load the program binary */

  ret = libmod_load(&loadinfo);
  mod_dumploadinfo(&loadinfo);
  if (ret != 0)
    {
      bdbg("Failed to load ELF program binary: %d\n", ret);
      goto errout_with_init;
    }

  /* Bind the program to the exported symbol table */

  ret = libmod_bind(&loadinfo, binp->exports, binp->nexports);
  if (ret != 0)
    {
      bdbg("Failed to bind symbols program binary: %d\n", ret);
      goto errout_with_load;
    }

  /* Return the load information */

  binp->entrypt   = NULL;
  binp->stacksize = 0;

  /* Add the ELF allocation to the alloc[] only if there is no address
   * environment.  If there is an address environment, it will automatically
   * be freed when the function exits
   *
   * REVISIT:  If the module is loaded then unloaded, wouldn't this cause
   * a memory leak?
   */

  binp->alloc[0]  = (FAR void *)loadinfo.textalloc;

#ifdef CONFIG_BINFMT_CONSTRUCTORS
  /* Save information about constructors.  NOTE:  destructors are not
   * yet supported.
   */

  binp->alloc[1]  = loadinfo.ctoralloc;
  binp->ctors     = loadinfo.ctors;
  binp->nctors    = loadinfo.nctors;

  binp->alloc[2]  = loadinfo.dtoralloc;
  binp->dtors     = loadinfo.dtors;
  binp->ndtors    = loadinfo.ndtors;
#endif

  /* Get the module initializer entry point */

  initializer = (mod_initializer_t)(loadinfo.textalloc + loadinfo.ehdr.e_entry);
  if (initialize)
    {
      mod_dumpinitializer(initializer, &loadinfo);

      /* Call the module initializer */

      ret = initializer();
      if (ret < 0)
        {
          bdbg("Failed to initialize the module: %d\n", ret);
          goto errout_with_load;
        }
    }

  libmod_uninitialize(&loadinfo);
  return OK;

errout_with_load:
  libmod_unload(&loadinfo);
errout_with_init:
  libmod_uninitialize(&loadinfo);
errout:
  return ret;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: mod_initialize
 *
 * Description:
 *   ELF support is built unconditionally.  However, in order to
 *   use this binary format, this function must be called during system
 *   initialization in order to register the ELF binary format.
 *
 * Returned Value:
 *   This is a NuttX internal function so it follows the convention that
 *   0 (OK) is returned on success and a negated errno is returned on
 *   failure.
 *
 ****************************************************************************/

int mod_initialize(void)
{
  int ret;

  /* Register ourselves as a binfmt loader */

  bvdbg("Registering ELF\n");

  ret = register_binfmt(&g_modbinfmt);
  if (ret != 0)
    {
      bdbg("Failed to register binfmt: %d\n", ret);
    }

  return ret;
}

/****************************************************************************
 * Name: mod_uninitialize
 *
 * Description:
 *   Unregister the ELF binary loader
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

void mod_uninitialize(void)
{
  unregister_binfmt(&g_modbinfmt);
}

#endif /* CONFIG_MODULE */
