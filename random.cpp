/*
Copyright (c) 2017, The University of Bristol, Senate House, Tyndall Avenue, Bristol, BS8 1TH, United Kingdom.
Copyright (c) 2021, COSIC-KU Leuven, Kasteelpark Arenberg 10, bus 2452, B-3001 Leuven-Heverlee, Belgium.

All rights reserved
*/

#include "random.h"

#include <iostream>
#include <stdio.h>
#include <string.h>

#include "cpu-support.h"
#include "util.h"

PRNG::PRNG() { useC= (cpu_has_aes() == false); }

void PRNG::ReSeed(int thread)
{
#ifdef DETERMINISTIC
  memset(seed, 0, sizeof(uint8_t) * SEED_SIZE);
#else
  FILE *rD= fopen("/dev/urandom", "r");
  if (fread(seed, sizeof(uint8_t), SEED_SIZE, rD) != SEED_SIZE)
    {
      throw std::runtime_error("fread on urandom gone wrong");
    }

  fclose(rD);
#endif
  uint8_t buff[4];
  INT_TO_BYTES(buff, thread);
  for (int i= 0; i < 4; i++)
    {
      seed[i]^= buff[i];
    }
  InitSeed();
}

void PRNG::SetSeedFromRandom(uint8_t *inp)
{
  memcpy(seed, inp, SEED_SIZE * sizeof(uint8_t));
  InitSeed();
}

void PRNG::SetSeed(PRNG &G)
{
  uint8_t tmp[SEED_SIZE];
  G.get_random_bytes(tmp, SEED_SIZE);
  SetSeedFromRandom(tmp);
}

void PRNG::InitSeed()
{
  if (useC)
    {
      aes_schedule(KeyScheduleC, seed);
    }
  else
    {
      aes_schedule(KeySchedule, seed);
    }
  memset(state, 0, RAND_SIZE * sizeof(uint8_t));
  for (int i= 0; i < PIPELINES; i++)
    state[i * AES_BLK_SIZE]= i;
  next();
  // cout << "SetSeed : "; print_state(); cout << endl;
}

void PRNG::print_state() const
{
  int i;
  for (i= 0; i < SEED_SIZE; i++)
    {
      if (seed[i] < 10)
        {
          cout << "0";
        }
      cout << hex << (int) seed[i];
    }
  cout << "\t";
  for (i= 0; i < RAND_SIZE; i++)
    {
      if (random[i] < 10)
        {
          cout << "0";
        }
      cout << hex << (int) random[i];
    }
  cout << "\t";
  for (i= 0; i < SEED_SIZE; i++)
    {
      if (state[i] < 10)
        {
          cout << "0";
        }
      cout << hex << (int) state[i];
    }
  cout << " " << dec << cnt << " : ";
}

void PRNG::hash()
{
  if (useC)
    {
      for (unsigned int i= 0; i < PIPELINES; i++)
        {
          aes_encrypt(random + i * AES_BLK_SIZE, state + i * AES_BLK_SIZE, KeyScheduleC);
        }
    }
  else
    {
      ecb_aes_128_encrypt<PIPELINES>((__m128i *) random, (__m128i *) state,
                                     KeySchedule);
    }
  // This is a new random value so we have not used any of it yet
  cnt= 0;
}

void PRNG::next()
{
  // Increment state
  for (int i= 0; i < PIPELINES; i++)
    {
      int64_t *s= (int64_t *) &state[i * AES_BLK_SIZE];
      s[0]+= PIPELINES;
      if (s[0] == 0)
        s[1]++;
    }
  hash();
}

double PRNG::get_double()
{
  // We need four bytes of randomness
  if (cnt > RAND_SIZE - 4)
    {
      next();
    }
  unsigned int a0= random[cnt], a1= random[cnt + 1], a2= random[cnt + 2],
               a3= random[cnt + 3];
  double ans= (a0 + (a1 << 8) + (a2 << 16) + (a3 << 24));
  cnt= cnt + 4;
  unsigned int den= 0xFFFFFFFF;
  ans= ans / den;
  // print_state(); cout << " DBLE " <<  ans << endl;
  return ans;
}

unsigned int PRNG::get_uint()
{
  // We need four bytes of randomness
  if (cnt > RAND_SIZE - 4)
    {
      next();
    }
  unsigned int ans= *((unsigned int *) &random[cnt]);
  cnt= cnt + 4;
  return ans;
}

int64_t PRNG::get_word()
{
  // We need eight bytes of randomness
  if (cnt > RAND_SIZE - 8)
    {
      next();
    }
  int64_t ans= *((int64_t *) &random[cnt]);
  cnt= cnt + 8;
  return ans;
}

__m128i PRNG::get_doubleword()
{
  if (cnt > RAND_SIZE - 16)
    {
      next();
    }
  __m128i ans= _mm_loadu_si128((__m128i *) &random[cnt]);
  cnt+= 16;
  return ans;
}

unsigned char PRNG::get_uchar()
{
  if (cnt >= RAND_SIZE)
    {
      next();
    }
  unsigned char ans= random[cnt];
  cnt++;
  // print_state(); cout << " UCHA " << (int) ans << endl;
  return ans;
}

/* Returns len random bytes */
void PRNG::get_random_bytes(uint8_t *ans, int len)
{
  int pos= 0;
  while (len)
    {
      int step= min(len, RAND_SIZE - cnt);
      memcpy(ans + pos, random + cnt, step);
      pos+= step;
      len-= step;
      cnt+= step;
      if (cnt == RAND_SIZE)
        next();
    }
}

/* Returns len random bytes */
void PRNG::get_random_bytes(vector<uint8_t> &ans)
{
  int pos= 0;
  int len= ans.size();
  while (len)
    {
      int step= min(len, RAND_SIZE - cnt);
      for (int i= 0; i < step; i++)
        {
          ans[pos + i]= random[cnt + i];
        }
      pos+= step;
      len-= step;
      cnt+= step;
      if (cnt == RAND_SIZE)
        next();
    }
}
