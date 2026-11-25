/*!
 * @file      smtc_hal_rng.c
 *
 * @brief     Random number HAL - STM32F411 (Black Pill) port.
 *
 * The STM32F411 has NO hardware RNG peripheral (the L476 has one). This implementation uses a
 * xoshiro128** PRNG, lazily seeded with:
 *   - the 96-bit unique device ID (different on every board -> the two Black Pills diverge),
 *   - the DWT cycle counter and the RTC sub-seconds (boot-time jitter: crystal vs. LSE),
 * and stirs the DWT cycle counter in at every call.
 *
 * Good enough for LoRa/LoRaWAN back-off, channel selection and DevNonce in a lab.
 * NOT a cryptographic RNG. For a stronger seed, feed radio noise with hal_rng_add_entropy()
 * (e.g. RSSI samples taken in RX) - TODO on the application side.
 *
 * The Clear BSD License
 * Copyright Semtech Corporation 2021. All rights reserved.
 */

#include <stdint.h>
#include <stdbool.h>

#include "stm32f4xx_hal.h"
#include "smtc_hal_rng.h"

static uint32_t rng_state[4];
static bool     rng_seeded = false;

static inline uint32_t rotl( const uint32_t x, int k )
{
    return ( x << k ) | ( x >> ( 32 - k ) );
}

static uint32_t xoshiro128ss_next( void )
{
    const uint32_t result = rotl( rng_state[1] * 5, 7 ) * 9;
    const uint32_t t      = rng_state[1] << 9;

    rng_state[2] ^= rng_state[0];
    rng_state[3] ^= rng_state[1];
    rng_state[1] ^= rng_state[2];
    rng_state[0] ^= rng_state[3];
    rng_state[2] ^= t;
    rng_state[3] = rotl( rng_state[3], 11 );

    return result;
}

// splitmix32-like mixer, used to spread the seed bits
static uint32_t mix32( uint32_t z )
{
    z = ( z ^ ( z >> 16 ) ) * 0x85EBCA6BU;
    z = ( z ^ ( z >> 13 ) ) * 0xC2B2AE35U;
    return z ^ ( z >> 16 );
}

static void rng_seed( void )
{
    const uint32_t uid0 = HAL_GetUIDw0( );
    const uint32_t uid1 = HAL_GetUIDw1( );
    const uint32_t uid2 = HAL_GetUIDw2( );
    const uint32_t jit  = DWT->CYCCNT ^ ( RTC->SSR << 16 ) ^ SysTick->VAL;

    rng_state[0] = mix32( uid0 ^ 0x9E3779B9U );
    rng_state[1] = mix32( uid1 ^ jit );
    rng_state[2] = mix32( uid2 ^ 0x7F4A7C15U );
    rng_state[3] = mix32( uid0 ^ uid1 ^ uid2 ^ ( jit * 0x2545F491U ) );
    if( ( rng_state[0] | rng_state[1] | rng_state[2] | rng_state[3] ) == 0 )
    {
        rng_state[0] = 1;  // xoshiro must not start from the all-zero state
    }
    rng_seeded = true;
}

void hal_rng_add_entropy( const uint32_t sample )
{
    if( !rng_seeded )
    {
        rng_seed( );
    }
    rng_state[0] ^= mix32( sample );
    ( void ) xoshiro128ss_next( );
}

uint32_t hal_rng_get_random( void )
{
    if( !rng_seeded )
    {
        rng_seed( );
    }
    rng_state[3] ^= DWT->CYCCNT;  // stir in execution-time jitter
    return xoshiro128ss_next( );
}

uint32_t hal_rng_get_random_in_range( const uint32_t val_1, const uint32_t val_2 )
{
    if( ( ( val_1 == 0 ) && ( val_2 == 0xFFFFFFFFU ) ) || ( ( val_2 == 0 ) && ( val_1 == 0xFFFFFFFFU ) ) )
    {
        return hal_rng_get_random( );  // full range: avoid "% 0"
    }
    if( val_1 <= val_2 )
    {
        return ( uint32_t ) ( ( hal_rng_get_random( ) % ( val_2 - val_1 + 1 ) ) + val_1 );
    }
    else
    {
        return ( uint32_t ) ( ( hal_rng_get_random( ) % ( val_1 - val_2 + 1 ) ) + val_2 );
    }
}

int32_t hal_rng_get_signed_random_in_range( const int32_t val_1, const int32_t val_2 )
{
    uint32_t tmp_range = 0;

    if( val_1 <= val_2 )
    {
        tmp_range = ( val_2 - val_1 );
        return ( int32_t ) ( ( val_1 + hal_rng_get_random_in_range( 0, tmp_range ) ) );
    }
    else
    {
        tmp_range = ( val_1 - val_2 );
        return ( int32_t ) ( ( val_2 + hal_rng_get_random_in_range( 0, tmp_range ) ) );
    }
}
