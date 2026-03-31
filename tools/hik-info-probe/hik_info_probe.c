#define _POSIX_C_SOURCE 200809L

#include "hik_probe.h"

#include <stdio.h>

int main( int argc, char **argv )
{
    HikOptions        opts;
    HikProfile        manual_profile;
    const HikProfile *profile   = NULL;
    HikTransportKind  transport = HIK_TRANSPORT_AUTO;
    HikSession        session;
    int               rc = 0;

    hik_options_init( &opts );
    if( !hik_parse_args( argc, argv, &opts ) )
    {
        hik_usage( stderr, argv[0] );
        return 2;
    }

    profile = hik_resolve_profile( &opts, &manual_profile );
    if( profile == NULL )
    {
        fprintf( stderr, "error: unknown profile: %s\n", opts.profile_name );
        hik_usage( stderr, argv[0] );
        return 2;
    }

    transport = hik_resolve_transport( &opts, profile );
    rc        = hik_validate_options( &opts, profile, transport );
    if( rc != 0 )
    {
        return rc;
    }

    rc = hik_open_session( &session, &opts, transport );
    if( rc != 0 )
    {
        return 1;
    }

    hik_print_connection_summary( &session, &opts );
    printf( "Using profile %s\n", profile->name );
    printf( "Using transport %s\n", hik_transport_name( transport ) );
    printf( "Using interface %u, unit 0x%02x, selector 0x%02x\n",
            opts.interface_number,
            opts.unit_id,
            profile->selector );

    rc = hik_execute_profile( &session, &opts, profile );
    hik_close_session( &session, &opts );
    
    return ( rc == 0 ) ? 0 : 1;
}
