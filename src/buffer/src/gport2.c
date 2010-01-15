/*
 * Copyright (c) 2007, 2009 Joseph Gaeddert
 * Copyright (c) 2007, 2009 Virginia Polytechnic Institute & State University
 *
 * This file is part of liquid.
 *
 * liquid is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * liquid is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with liquid.  If not, see <http://www.gnu.org/licenses/>.
 */

//
// Generic port
//

#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>
#include <pthread.h>
#include <string.h>

#include "liquid.internal.h"

struct gport2_s {
    void * v;
    unsigned int n;     // buffer size (elements)
    size_t size;        // sizeof(element)

    // producer
    unsigned int write_index;
    pthread_mutex_t producer_mutex;
    unsigned int num_write_elements_available;
    pthread_cond_t producer_data_ready;
    bool producer_waiting;

    // consumer
    unsigned int read_index;
    pthread_mutex_t consumer_mutex;
    unsigned int num_read_elements_available;
    pthread_cond_t consumer_data_ready;
    bool consumer_waiting;

    // other
    pthread_mutex_t internal_mutex;
};

gport2 gport2_create(unsigned int _n, unsigned int _size)
{
    // validate input
    if (_n == 0) {
        fprintf(stderr,"error: gport2_create(), buffer length cannot be zero\n");
        exit(1);
    } else if (_size == 0) {
        fprintf(stderr,"error: gport2_create(), object size cannot be zero\n");
        exit(1);
    }

    gport2 p = (gport2) malloc(sizeof(struct gport2_s));
    p->v = NULL;

    p->n = _n;
    p->size = (size_t)_size;
    p->v = (void*) malloc((p->n)*(p->size));

    // producer
    pthread_mutex_init(&p->producer_mutex,NULL);
    p->write_index = 0;
    p->num_write_elements_available = p->n;
    //p->num_write_elements_requested = 0;
    pthread_cond_init(&p->producer_data_ready,NULL);

    // consumer
    pthread_mutex_init(&p->consumer_mutex,NULL);
    p->read_index = 0;
    p->num_read_elements_available = 0;
    //p->num_read_elements_requested = 0;
    pthread_cond_init(&p->consumer_data_ready,NULL);

    // internal
    pthread_mutex_init(&p->internal_mutex,NULL);

    return p;
}

void gport2_destroy(gport2 _p)
{
    // producer
    pthread_mutex_destroy(&_p->producer_mutex);
    pthread_cond_destroy(&_p->producer_data_ready);

    // consumer
    pthread_mutex_destroy(&_p->consumer_mutex);
    pthread_cond_destroy(&_p->consumer_data_ready);

    // other
    pthread_mutex_destroy(&_p->internal_mutex);
    free(_p->v);
    free(_p);
}

void gport2_print(gport2 _p)
{
    pthread_mutex_lock(&_p->internal_mutex);

    printf("gport2: [%u @ %u bytes]\n", _p->n, (unsigned int) (_p->size));
    unsigned int i,j,n=0;
    unsigned char * s = (unsigned char*) (_p->v);
    for (i=0; i<_p->n; i++) {
        printf("  %3u:  0x", i);
        for (j=0; j<_p->size; j++) {
            printf("%.2x", s[n++]);
        }
        printf("\n");
    }

    pthread_mutex_unlock(&_p->internal_mutex);
}

void gport2_produce(gport2 _p, void * _w, unsigned int _n)
{
    unsigned int num_produced;
    unsigned int num_produced_total = 0;

    // produce samples as they become available
    while (_n > 0) {
        // produce maximum number of samples available
        gport2_produce_available(_p,
                                 _w + num_produced_total*_p->size,
                                 _n,
                                 &num_produced);
        // update counters
        num_produced_total += num_produced;
        _n -= num_produced;
    }
}

void gport2_produce_available(gport2 _p,
                              void * _w,
                              unsigned int _nmax,
                              unsigned int *_np)
{
    pthread_mutex_lock(&(_p->producer_mutex));
    pthread_mutex_lock(&(_p->internal_mutex));

    while (_p->num_write_elements_available == 0) {
        _p->producer_waiting = true;
        pthread_cond_wait(&(_p->producer_data_ready),&(_p->internal_mutex));
    }
    _p->producer_waiting = false;

    unsigned int n = (_p->num_write_elements_available > _nmax) ?
        _nmax : _p->num_write_elements_available;
    *_np = n;

    // copy data circularly if necessary
    if (_p->write_index + n > _p->n) {
        // overflow: copy data circularly
        unsigned int b = _p->n - _p->write_index;

        // copy lower section: 'b' elements
        memmove(_p->v + (_p->write_index)*(_p->size),
                _w,
                b*(_p->size));
        
        // copy upper section
        memmove(_p->v,
                _w + b*(_p->size),
                (n-b)*(_p->size));
    } else {
        memmove(_p->v + (_p->write_index)*(_p->size),
                _w,
                n*(_p->size));
    }

    _p->num_write_elements_available -= n;
    _p->num_read_elements_available += n;

    _p->write_index = (_p->write_index + n) % _p->n;

    // signal consumer
    if (_p->consumer_waiting) {
        pthread_cond_signal(&(_p->consumer_data_ready));
    }

    pthread_mutex_unlock(&(_p->internal_mutex));
    pthread_mutex_unlock(&(_p->producer_mutex));
}

void gport2_consume(gport2 _p, void * _r, unsigned int _n)
{
    unsigned int num_consumed;
    unsigned int num_consumed_total = 0;

    // consume samples as they become available
    while (_n > 0) {
        // consume maximum number of samples available
        gport2_consume_available(_p,
                                 _r + num_consumed_total*_p->size,
                                 _n,
                                 &num_consumed);
        // update counters
        num_consumed_total += num_consumed;
        _n -= num_consumed;
    }
}

void gport2_consume_available(gport2 _p, void * _r, unsigned int _nmax, unsigned int *_nc)
{
    pthread_mutex_lock(&(_p->consumer_mutex));
    pthread_mutex_lock(&(_p->internal_mutex));

    while (_p->num_read_elements_available == 0) {
        _p->consumer_waiting = true;
        pthread_cond_wait(&(_p->consumer_data_ready),&(_p->internal_mutex));
    }
    _p->consumer_waiting = false;

    unsigned int n = (_p->num_read_elements_available > _nmax) ?
        _nmax : _p->num_read_elements_available;
    *_nc = n;

    // copy data circularly if necessary
    if (n > _p->n - _p->read_index) {
        // underflow: copy data circularly
        unsigned int b = _p->n - _p->read_index;

        // copy lower section: 'b' elements
        memmove(_r,
                _p->v + (_p->read_index)*(_p->size),
                b*(_p->size));

        // copy upper section
        memmove(_r + b*(_p->size),
                _p->v,
                (n-b)*(_p->size));

    } else {
        memmove(_r,
                _p->v + (_p->read_index)*(_p->size),
                n*(_p->size));
    }

    _p->num_read_elements_available -= n;
    _p->num_write_elements_available += n;

    _p->read_index = (_p->read_index + n) % _p->n;

    // signal producer
    if (_p->producer_waiting) {
        pthread_cond_signal(&(_p->producer_data_ready));
    }

    pthread_mutex_unlock(&(_p->internal_mutex));
    pthread_mutex_unlock(&(_p->consumer_mutex));
}

