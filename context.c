/*
 * context.c
 *
 * Manage context structure.
 *
 * The context structure is the internal state maintained by the pam_krb5
 * module between calls to the various public interfaces.
 */

#include "config.h"

#include <security/pam_appl.h>
#include <security/pam_modules.h>
#include <stdlib.h>
#include <string.h>

#include "internal.h"

/* Solaris doesn't have these. */
#ifndef PAM_CONV_AGAIN
# define PAM_CONV_AGAIN 0
# define PAM_INCOMPLETE PAM_SERVICE_ERR
#endif

/*
 * Create a new context and populate it with the user from PAM and a new
 * Kerberos context.  Set the default realm if one was configured.
 */
int
pamk5_context_new(struct pam_args *args)
{
    struct context *ctx;
    int retval;
    PAM_CONST char *name;

    ctx = calloc(1, sizeof(struct context));
    if (ctx == NULL) {
        retval = PAM_BUF_ERR;
        goto done;
    }
    ctx->creds = NULL;
    args->ctx = ctx;

    /*
     * This will prompt for the username if it's not already set (generally it
     * will be).  Otherwise, grab the saved username.
     */
    retval = pam_get_user(args->pamh, &name, NULL);
    if (retval != PAM_SUCCESS || name == NULL) {
        if (retval == PAM_CONV_AGAIN)
            retval = PAM_INCOMPLETE;
        else
            retval = PAM_SERVICE_ERR;
        goto done;
    }
    ctx->name = strdup(name);
    retval = krb5_init_context(&ctx->context);
    if (retval != 0) {
        pamk5_error_krb5(args, "krb5_init_context", retval);
        retval = PAM_SERVICE_ERR;
        goto done;
    }

    /* Set a default realm if one was configured. */
    if (args->realm != NULL) {
        retval = krb5_set_default_realm(ctx->context, args->realm);
        if (retval != 0) {
            pamk5_error_krb5(args, "cannot set default realm", retval);
            retval = PAM_SERVICE_ERR;
            goto done;
        }
    }

done:
    if (ctx != NULL && retval != PAM_SUCCESS) {
        pamk5_context_free(ctx);
        args->ctx = NULL;
    }
    return retval;
}


/*
 * Retrieve a context from the PAM data structures, returning failure if no
 * context was present.  Note that OpenSSH loses contexts between authenticate
 * and setcred, so failure shouldn't always be fatal.
 */
int
pamk5_context_fetch(struct pam_args *args)
{
    int pamret;

    pamret = pam_get_data(args->pamh, "ctx", (void *) &args->ctx);
    if (pamret != PAM_SUCCESS)
        args->ctx = NULL;
    return (pamret == 0 && args->ctx == NULL) ? PAM_SERVICE_ERR : pamret;
}


/*
 * Free a context and all of the data that's stored in it.  Normally this also
 * includes destroying the ticket cache, but don't do this (just close it) if
 * a flag was set to preserve it.
 */
void
pamk5_context_free(struct context *ctx)
{
    if (ctx == NULL)
        return;
    if (ctx->context != NULL) {
        if (ctx->name != NULL)
            free(ctx->name);
        if (ctx->princ != NULL)
            krb5_free_principal(ctx->context, ctx->princ);
        if (ctx->cache != NULL) {
            if (ctx->dont_destroy_cache)
                krb5_cc_close(ctx->context, ctx->cache);
            else
                krb5_cc_destroy(ctx->context, ctx->cache);
        }
        if (ctx->creds != NULL) {
            krb5_free_cred_contents(ctx->context, ctx->creds);
            free(ctx->creds);
        }
        krb5_free_context(ctx->context);
    }
    free(ctx);
}


/*
 * The PAM callback to destroy the context stored in the PAM data structures.
 * Just does the necessary conversion of arguments and calls
 * pamk5_context_free.
 */
void
pamk5_context_destroy(pam_handle_t *pamh, void *data, int pam_end_status)
{
    struct context *ctx = (struct context *) data;

    if (ctx != NULL)
        pamk5_context_free(ctx);
}
