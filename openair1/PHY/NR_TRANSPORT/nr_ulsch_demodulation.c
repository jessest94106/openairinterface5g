#include "PHY/defs_gNB.h"
#include "PHY/phy_extern.h"
#include "nr_transport_proto.h"
#include "PHY/impl_defs_top.h"
#include "PHY/NR_TRANSPORT/nr_sch_dmrs.h"
#include "PHY/NR_REFSIG/dmrs_nr.h"
#include "PHY/NR_REFSIG/ptrs_nr.h"
#include "PHY/NR_ESTIMATION/nr_ul_estimation.h"
#include "PHY/defs_nr_common.h"
#include "PHY/nr_phy_common/inc/nr_phy_common.h"
#include "common/utils/nr/nr_common.h"
#include <openair1/PHY/TOOLS/phy_scope_interface.h>
#include "PHY/sse_intrin.h"
#include "T.h"
#include <sys/time.h>
#include "PHY/log_tools.h"
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <sched.h>
#include <unistd.h>


#define GNB_PUSCH_RT_TRACE_ENABLE 0

#if GNB_PUSCH_RT_TRACE_ENABLE
#define GNB_PUSCH_RT_TRACE_CAP 8192
#define GNB_PUSCH_RT_TRACE_RE 8
#define GNB_PUSCH_RT_TRACE_HARD_BYTES 96

typedef struct {
  int frame;
  int slot;
  uint16_t rnti;
  uint8_t harq_pid;
  uint8_t round;
  uint8_t rv;
  uint16_t rb_start;
  uint16_t rb_size;
  uint8_t symbol;
  uint8_t dmrs;
  uint8_t qam;
  uint8_t mcs;
  uint32_t tb_size;
  int64_t raw_energy;
  int64_t ch_energy;
  int64_t comp_energy;
  uint16_t hard_bits;
  uint32_t comp_sign_hash;
  uint8_t hard[GNB_PUSCH_RT_TRACE_HARD_BYTES];
  c16_t raw[GNB_PUSCH_RT_TRACE_RE];
  c16_t ch[GNB_PUSCH_RT_TRACE_RE];
  c16_t comp[GNB_PUSCH_RT_TRACE_RE];
} gnb_pusch_rt_trace_t;

static gnb_pusch_rt_trace_t gnb_pusch_rt_trace[GNB_PUSCH_RT_TRACE_CAP];
static unsigned int gnb_pusch_rt_trace_count;
static unsigned int gnb_pusch_rt_trace_last_dump_count;

static void dump_gnb_pusch_rt_trace(void)
{
  FILE *f = fopen("/tmp/oai_gnb_pusch_rt_trace.csv", "w");
  if (f == NULL)
    return;

  fprintf(f, "idx,frame,slot,rnti,harq,round,rv,rb_start,rb_size,symbol,dmrs,qam,mcs,tb_size,raw_energy,ch_energy,comp_energy,hard_bits,comp_sign_hash,hard_hex");
  for (int re = 0; re < GNB_PUSCH_RT_TRACE_RE; re++)
    fprintf(f, ",raw%d_r,raw%d_i,ch%d_r,ch%d_i,comp%d_r,comp%d_i", re, re, re, re, re, re);
  fprintf(f, "\n");

  unsigned int n = gnb_pusch_rt_trace_count < GNB_PUSCH_RT_TRACE_CAP ? gnb_pusch_rt_trace_count : GNB_PUSCH_RT_TRACE_CAP;
  for (unsigned int i = 0; i < n; i++) {
    const gnb_pusch_rt_trace_t *t = &gnb_pusch_rt_trace[i];
    fprintf(f, "%u,%d,%d,%04x,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%ld,%ld,%ld,%u,%08x,",
            i,
            t->frame,
            t->slot,
            t->rnti,
            t->harq_pid,
            t->round,
            t->rv,
            t->rb_start,
            t->rb_size,
            t->symbol,
            t->dmrs,
            t->qam,
            t->mcs,
            t->tb_size,
            (long)t->raw_energy,
            (long)t->ch_energy,
            (long)t->comp_energy,
            t->hard_bits,
            t->comp_sign_hash);
    int hard_bytes = (t->hard_bits + 7) >> 3;
    if (hard_bytes > GNB_PUSCH_RT_TRACE_HARD_BYTES)
      hard_bytes = GNB_PUSCH_RT_TRACE_HARD_BYTES;
    for (int b = 0; b < hard_bytes; b++)
      fprintf(f, "%02x", t->hard[b]);
    for (int re = 0; re < GNB_PUSCH_RT_TRACE_RE; re++)
      fprintf(f, ",%d,%d,%d,%d,%d,%d",
              t->raw[re].r,
              t->raw[re].i,
              t->ch[re].r,
              t->ch[re].i,
              t->comp[re].r,
              t->comp[re].i);
    fprintf(f, "\n");
  }
  fclose(f);
}

static void *gnb_pusch_rt_trace_dump_thread(void *unused)
{
  (void)unused;
  for (;;) {
    sleep(1);
    unsigned int count = __atomic_load_n(&gnb_pusch_rt_trace_count, __ATOMIC_RELAXED);
    if (count != gnb_pusch_rt_trace_last_dump_count) {
      dump_gnb_pusch_rt_trace();
      gnb_pusch_rt_trace_last_dump_count = count;
    }
  }
  return NULL;
}

static void start_gnb_pusch_rt_trace_dump_thread(void)
{
  pthread_t thread;
  pthread_attr_t attr;
  pthread_attr_init(&attr);
  pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
  pthread_attr_setinheritsched(&attr, PTHREAD_EXPLICIT_SCHED);
  pthread_attr_setschedpolicy(&attr, SCHED_OTHER);
  struct sched_param sp = {0};
  pthread_attr_setschedparam(&attr, &sp);
  if (pthread_create(&thread, &attr, gnb_pusch_rt_trace_dump_thread, NULL) != 0)
    atexit(dump_gnb_pusch_rt_trace);
  pthread_attr_destroy(&attr);
}

static void init_gnb_pusch_rt_trace_dump(void) __attribute__((constructor));
static void init_gnb_pusch_rt_trace_dump(void)
{
  atexit(dump_gnb_pusch_rt_trace);
  start_gnb_pusch_rt_trace_dump_thread();
}
#endif

static void record_gnb_pusch_rt_trace(int frame,
                                      int slot,
                                      uint16_t rnti,
                                      uint8_t harq_pid,
                                      uint8_t round,
                                      const nfapi_nr_pusch_pdu_t *rel15_ul,
                                      uint8_t symbol,
                                      const c16_t *raw,
                                      const c16_t *ch,
                                      const c16_t *comp,
                                      int valid_re)
{
#if GNB_PUSCH_RT_TRACE_ENABLE
  unsigned int idx = __atomic_fetch_add(&gnb_pusch_rt_trace_count, 1, __ATOMIC_RELAXED);
  if (idx >= GNB_PUSCH_RT_TRACE_CAP)
    return;

  gnb_pusch_rt_trace_t *t = &gnb_pusch_rt_trace[idx];
  *t = (gnb_pusch_rt_trace_t){0};
  t->frame = frame;
  t->slot = slot;
  t->rnti = rnti;
  t->harq_pid = harq_pid;
  t->round = round;
  t->rv = rel15_ul->pusch_data.rv_index;
  t->rb_start = rel15_ul->rb_start;
  t->rb_size = rel15_ul->rb_size;
  t->symbol = symbol;
  t->dmrs = (rel15_ul->ul_dmrs_symb_pos >> symbol) & 0x01;
  t->qam = rel15_ul->qam_mod_order;
  t->mcs = rel15_ul->mcs_index;
  t->tb_size = rel15_ul->pusch_data.tb_size;

  uint32_t sign_hash = 2166136261u;
  int bit = 0;
  for (int re = 0; re < valid_re; re++) {
    c16_t r = raw[re];
    c16_t h = ch[re];
    c16_t c = comp[re];
    if (re < GNB_PUSCH_RT_TRACE_RE) {
      t->raw[re] = r;
      t->ch[re] = h;
      t->comp[re] = c;
    }
    t->raw_energy += (int64_t)r.r * r.r + (int64_t)r.i * r.i;
    t->ch_energy += (int64_t)h.r * h.r + (int64_t)h.i * h.i;
    t->comp_energy += (int64_t)c.r * c.r + (int64_t)c.i * c.i;
    uint8_t code = 0;
    if (c.r < 0)
      code |= 1;
    if (c.i < 0)
      code |= 2;
    sign_hash = (sign_hash ^ code) * 16777619u;
    if (bit < GNB_PUSCH_RT_TRACE_HARD_BYTES * 8 && (code & 1))
      t->hard[bit >> 3] |= 1u << (bit & 7);
    bit++;
    if (bit < GNB_PUSCH_RT_TRACE_HARD_BYTES * 8 && (code & 2))
      t->hard[bit >> 3] |= 1u << (bit & 7);
    bit++;
  }
  t->hard_bits = bit;
  t->comp_sign_hash = sign_hash;
#else
  (void)frame;
  (void)slot;
  (void)rnti;
  (void)harq_pid;
  (void)round;
  (void)rel15_ul;
  (void)symbol;
  (void)raw;
  (void)ch;
  (void)comp;
  (void)valid_re;
#endif
}


#if T_TRACER
static void copy_c16_data_to_slot_memory(c16_t *src, c16_t *dst_slot, int nb_re_pusch, int symbol)
{
  memcpy(&dst_slot[nb_re_pusch * symbol], src, nb_re_pusch * sizeof(c16_t));
}
#endif

void nr_idft(int32_t *z, uint32_t Msc_PUSCH)
{

  simde__m128i idft_in128[1][3240], idft_out128[1][3240];
  simde__m128i norm128;
  int16_t *idft_in0 = (int16_t*)idft_in128[0], *idft_out0 = (int16_t*)idft_out128[0];

  int i, ip;

  LOG_T(PHY,"Doing nr_idft for Msc_PUSCH %d\n", Msc_PUSCH);

  if ((Msc_PUSCH % 1536) > 0) {
    // conjugate input
    for (i = 0; i < (Msc_PUSCH>>2); i++) {
      ((simde__m128i*)z)[i] = oai_mm_conj( ((simde__m128i*)z)[i] );
    }
    for (i = 0, ip = 0; i < Msc_PUSCH; i++, ip+=4)
      ((uint32_t*)idft_in0)[ip+0] = z[i];
  }
  dft_size_idx_t dftsize = get_dft(Msc_PUSCH);
  switch (Msc_PUSCH) {
    case 12:
      dft(dftsize, (int16_t *)idft_in0, (int16_t *)idft_out0, 0);

      norm128 = simde_mm_set1_epi16(9459);

      for (i = 0; i < 12; i++) {
        ((simde__m128i *)idft_out0)[i] = simde_mm_slli_epi16(simde_mm_mulhi_epi16(((simde__m128i *)idft_out0)[i], norm128), 1);
      }

      break;
    default:
      dft(dftsize, idft_in0, idft_out0, 1);
      break;
  }

  if ((Msc_PUSCH % 1536) > 0) {
    for (i = 0, ip = 0; i < Msc_PUSCH; i++, ip+=4)
      z[i] = ((uint32_t*)idft_out0)[ip];

    // conjugate output
    for (i = 0; i < (Msc_PUSCH>>2); i++) {
      ((simde__m128i*)z)[i] = oai_mm_conj(((simde__m128i*)z)[i]);
    }
  }
}

static void nr_ulsch_extract_rbs(c16_t* const rxdataF,
                                 c16_t* const chF,
                                 c16_t *rxFext,
                                 c16_t *chFext,
                                 int rxoffset,
                                 int choffset,
                                 int aarx,
                                 int is_dmrs_symbol,
                                 nfapi_nr_pusch_pdu_t *pusch_pdu,
                                 NR_DL_FRAME_PARMS *frame_parms)
{
  uint8_t delta = 0;
  int start_re = (frame_parms->first_carrier_offset + (pusch_pdu->rb_start + pusch_pdu->bwp_start) * NR_NB_SC_PER_RB)%frame_parms->ofdm_symbol_size;
  int nb_re_pusch = NR_NB_SC_PER_RB * pusch_pdu->rb_size;
  c16_t *rxF = &rxdataF[rxoffset];
  c16_t *rxF_ext = &rxFext[0];
  c16_t *ul_ch0 = &chF[choffset];
  c16_t *ul_ch0_ext = &chFext[0];

  if (is_dmrs_symbol == 0) {
    if (start_re + nb_re_pusch <= frame_parms->ofdm_symbol_size)
      memcpy(rxF_ext, &rxF[start_re], nb_re_pusch * sizeof(c16_t));
    else {
      int neg_length = frame_parms->ofdm_symbol_size - start_re;
      int pos_length = nb_re_pusch - neg_length;
      memcpy(rxF_ext, &rxF[start_re], neg_length * sizeof(c16_t));
      memcpy(&rxF_ext[neg_length], rxF, pos_length * sizeof(c16_t));
    }
    memcpy(ul_ch0_ext, ul_ch0, nb_re_pusch * sizeof(c16_t));
  }
  else if (pusch_pdu->dmrs_config_type == pusch_dmrs_type1) { // 6 REs / PRB
    AssertFatal(delta == 0 || delta == 1, "Illegal delta %d\n",delta);
    c16_t *rxF32 = &rxF[start_re];
    if (start_re + nb_re_pusch < frame_parms->ofdm_symbol_size) {
      for (int idx = 1 - delta; idx < nb_re_pusch; idx += 2) {
        *rxF_ext++ = rxF32[idx];
        *ul_ch0_ext++ = ul_ch0[idx];
      }
    }
    else { // handle the two pieces around DC
      int neg_length = frame_parms->ofdm_symbol_size - start_re;
      int pos_length = nb_re_pusch - neg_length;
      int idx, idx2;
      for (idx = 1 - delta; idx < neg_length; idx += 2) {
        *rxF_ext++ = rxF32[idx];
        *ul_ch0_ext++= ul_ch0[idx];
      }
      rxF32 = rxF;
      idx2 = idx;
      for (idx = 1 - delta; idx < pos_length; idx += 2, idx2 += 2) {
        *rxF_ext++ = rxF32[idx];
        *ul_ch0_ext++ = ul_ch0[idx2];
      }
    }
  }
  else if (pusch_pdu->dmrs_config_type == pusch_dmrs_type2) { // 8 REs / PRB
    AssertFatal(delta==0||delta==2||delta==4,"Illegal delta %d\n",delta);
    if (start_re + nb_re_pusch < frame_parms->ofdm_symbol_size) {
      for (int idx = 0; idx < nb_re_pusch; idx ++) {
        if (idx % 6 == 2 * delta || idx % 6 == 2 * delta + 1)
          continue;
        *rxF_ext++ = rxF[idx];
        *ul_ch0_ext++ = ul_ch0[idx];
      }
    }
    else {
      int neg_length = frame_parms->ofdm_symbol_size - start_re;
      int pos_length = nb_re_pusch - neg_length;
      c16_t *rxF64 = &rxF[start_re];
      int idx, idx2;
      for (idx = 0; idx < neg_length; idx ++) {
        if (idx % 6 == 2 * delta || idx % 6 == 2 * delta + 1)
          continue;
        *rxF_ext++ = rxF64[idx];
        *ul_ch0_ext++ = ul_ch0[idx];
      }
      rxF64 = rxF;
      idx2 = idx;
      for (idx = 0; idx < pos_length; idx++, idx2++) {
        if (idx % 6 == 2 * delta || idx % 6 == 2 * delta + 1)
          continue;
        *rxF_ext++ = rxF64[idx];
        *ul_ch0_ext++ = ul_ch0[idx2];
      }
    }
  }
}

static int get_nb_re_pusch (NR_DL_FRAME_PARMS *frame_parms, nfapi_nr_pusch_pdu_t *rel15_ul,int symbol) 
{
  uint8_t dmrs_symbol_flag = (rel15_ul->ul_dmrs_symb_pos >> symbol) & 0x01;
  if (dmrs_symbol_flag == 1) {
    if ((rel15_ul->ul_dmrs_symb_pos >> ((symbol + 1) % frame_parms->symbols_per_slot)) & 0x01)
      AssertFatal(1==0,"Double DMRS configuration is not yet supported\n");

    if (rel15_ul->dmrs_config_type == 0) {
      // if no data in dmrs cdm group is 1 only even REs have no data
      // if no data in dmrs cdm group is 2 both odd and even REs have no data
      return(rel15_ul->rb_size *(12 - (rel15_ul->num_dmrs_cdm_grps_no_data*6)));
    }
    else return(rel15_ul->rb_size *(12 - (rel15_ul->num_dmrs_cdm_grps_no_data*4)));
  } else return(rel15_ul->rb_size * NR_NB_SC_PER_RB);
}

static void nr_ulsch_channel_compensation(uint32_t buffer_length,
                                          int nb_rx_ant,
                                          c16_t rxFext[][buffer_length],
                                          c16_t chFext[][nb_rx_ant][buffer_length],
                                          c16_t ul_ch_maga[][buffer_length],
                                          c16_t ul_ch_magb[][buffer_length],
                                          c16_t ul_ch_magc[][buffer_length],
                                          int32_t **rxComp,
                                          int nb_layers,
                                          c16_t rho[][nb_layers][buffer_length],
                                          nfapi_nr_pusch_pdu_t *rel15_ul,
                                          uint32_t symbol,
                                          uint32_t output_shift)
{
  int mod_order  = rel15_ul->qam_mod_order;
  int nrOfLayers = rel15_ul->nrOfLayers;

  simde__m256i QAM_ampa_256 = simde_mm256_setzero_si256();
  simde__m256i QAM_ampb_256 = simde_mm256_setzero_si256();
  simde__m256i QAM_ampc_256 = simde_mm256_setzero_si256();

  if (mod_order == 4) {
    QAM_ampa_256 = simde_mm256_set1_epi16(QAM16_n1);
    QAM_ampb_256 = simde_mm256_setzero_si256();
    QAM_ampc_256 = simde_mm256_setzero_si256();
  }
  else if (mod_order == 6) {
    QAM_ampa_256 = simde_mm256_set1_epi16(QAM64_n1);
    QAM_ampb_256 = simde_mm256_set1_epi16(QAM64_n2);
    QAM_ampc_256 = simde_mm256_setzero_si256();
  }
  else if (mod_order == 8) {
    QAM_ampa_256 = simde_mm256_set1_epi16(QAM256_n1);
    QAM_ampb_256 = simde_mm256_set1_epi16(QAM256_n2);
    QAM_ampc_256 = simde_mm256_set1_epi16(QAM256_n3);
  }

  for (int aatx = 0; aatx < nrOfLayers; aatx++) {
    simde__m256i *rxComp_256 = (simde__m256i *)&rxComp[aatx * nb_rx_ant][symbol * buffer_length];
    simde__m256i *rxF_ch_maga_256 = (simde__m256i *)ul_ch_maga[aatx];
    simde__m256i *rxF_ch_magb_256 = (simde__m256i *)ul_ch_magb[aatx];
    simde__m256i *rxF_ch_magc_256 = (simde__m256i *)ul_ch_magc[aatx];
    for (int aarx = 0; aarx < nb_rx_ant; aarx++) {
      simde__m256i *rxF_256 = (simde__m256i *)rxFext[aarx];
      simde__m256i *chF_256 = (simde__m256i *)chFext[aatx][aarx];

      for (int i = 0; i < buffer_length >> 3; i++) 
      {
        // MRC        
        simde__m256i comp = oai_mm256_cpx_mult_conj(chF_256[i], rxF_256[i], output_shift);
        rxComp_256[i] = simde_mm256_add_epi16(rxComp_256[i], comp); 

        if (mod_order > 2) {
          simde__m256i mag = oai_mm256_smadd(chF_256[i], chF_256[i], output_shift); // |h|^2
          // pack and duplicate
          mag = simde_mm256_packs_epi32(mag, mag);
          mag = simde_mm256_unpacklo_epi16(mag, mag);

          rxF_ch_maga_256[i] = simde_mm256_add_epi16(rxF_ch_maga_256[i], simde_mm256_mulhrs_epi16(mag, QAM_ampa_256));

          if (mod_order > 4)
            rxF_ch_magb_256[i] = simde_mm256_add_epi16(rxF_ch_magb_256[i], simde_mm256_mulhrs_epi16(mag, QAM_ampb_256));

          if (mod_order > 6)
            rxF_ch_magc_256[i] = simde_mm256_add_epi16(rxF_ch_magc_256[i], simde_mm256_mulhrs_epi16(mag, QAM_ampc_256));
        }        
      }
      if (nb_layers > 1) {
        for (int atx = 0; atx < nrOfLayers; atx++) {
          simde__m256i *rho_256 = (simde__m256i *)rho[aatx][atx];
          simde__m256i *chF_256 = (simde__m256i *)chFext[aatx][aarx];
          simde__m256i *chF2_256 = (simde__m256i *)chFext[atx][aarx];
          for (int i = 0; i < buffer_length >> 3; i++) {
            rho_256[i] = simde_mm256_adds_epi16(rho_256[i], oai_mm256_cpx_mult_conj(chF_256[i], chF2_256[i], output_shift));
          }
        }
      }
    }
  }

}

// Zero Forcing Rx function: nr_det_HhH()
static void nr_ulsch_det_HhH(c16_t *after_mf_00, // a
                             c16_t *after_mf_01, // b
                             c16_t *after_mf_10, // c
                             c16_t *after_mf_11, // d
                             uint32_t *det_fin, // 1/ad-bc
                             unsigned short nb_rb,
                             unsigned char symbol,
                             int32_t shift)
{
  simde__m128i *after_mf_00_128,*after_mf_01_128, *after_mf_10_128, *after_mf_11_128, ad_re_128, bc_re_128; //ad_im_128, bc_im_128;
  simde__m128i *det_fin_128, det_re_128; //det_im_128, tmp_det0, tmp_det1;

  after_mf_00_128 = (simde__m128i *)after_mf_00;
  after_mf_01_128 = (simde__m128i *)after_mf_01;
  after_mf_10_128 = (simde__m128i *)after_mf_10;
  after_mf_11_128 = (simde__m128i *)after_mf_11;

  det_fin_128 = (simde__m128i *)det_fin;

  for (unsigned short rb=0; rb<3*nb_rb; rb++) {

    //complex multiplication (I_a+jQ_a)(I_d+jQ_d) = (I_aI_d - Q_aQ_d) + j(Q_aI_d + I_aQ_d)
    //The imag part is often zero, we compute only the real part
    ad_re_128 = simde_mm_madd_epi16(oai_mm_conj(after_mf_00_128[0]),after_mf_11_128[0]); //Re: I_a0*I_d0 - Q_a1*Q_d1
    //ad_im_128 = simde_mm_madd_epi16(oai_mm_swap(after_mf_00_128[0]),after_mf_11_128[0]);//Im: (Q_aI_d + I_aQ_d)

    //complex multiplication (I_b+jQ_b)(I_c+jQ_c) = (I_bI_c - Q_bQ_c) + j(Q_bI_c + I_bQ_c)
    //The imag part is often zero, we compute only the real part
    bc_re_128 = simde_mm_madd_epi16(oai_mm_conj(after_mf_01_128[0]),after_mf_10_128[0]); //Re: I_b0*I_c0 - Q_b1*Q_c1
    //bc_im_128 = simde_mm_madd_epi16(oai_mm_swap(after_mf_01_128[0]),after_mf_10_128[0]);//Im: (Q_bI_c + I_bQ_c)

    det_re_128 = simde_mm_sub_epi32(ad_re_128, bc_re_128);
    //det_im_128 = simde_mm_sub_epi32(ad_im_128, bc_im_128);

    //det in Q30 format
    det_fin_128[0] = simde_mm_abs_epi32(det_re_128);


#ifdef DEBUG_DLSCH_DEMOD
     printf("\n Computing det_HhH_inv \n");
     //print_ints("det_re_128:",(int32_t*)&det_re_128);
     //print_ints("det_im_128:",(int32_t*)&det_im_128);
     print_ints("det_fin_128:",(int32_t*)&det_fin_128[0]);
#endif
    det_fin_128+=1;
    after_mf_00_128+=1;
    after_mf_01_128+=1;
    after_mf_10_128+=1;
    after_mf_11_128+=1;
  }
}

/* Zero Forcing Rx function: nr_conjch0_mult_ch1()
 *
 *
 * */
// TODO: This function is just a wrapper, can be removed.
static void nr_ulsch_conjch0_mult_ch1(c16_t *ch0, c16_t *ch1, c16_t *ch0conj_ch1, unsigned short nb_rb, unsigned char output_shift0)
{
  //This function is used to compute multiplications in H_hermitian * H matrix
  mult_cpx_conj_vector(ch0, ch1, ch0conj_ch1, 12 * nb_rb, output_shift0);
}

static simde__m128i nr_ulsch_comp_muli_sum(simde__m128i input_x,
                                           simde__m128i input_y,
                                           simde__m128i input_w,
                                           simde__m128i input_z,
                                           simde__m128i det)
{

  // complex multiplication (x_re + jx_im)*(y_re + jy_im) = (x_re*y_re - x_im*y_im) + j(x_im*y_re + x_re*y_im)
  // complex multiplication (w_re + jw_im)*(z_re + jz_im) = (w_re*z_re - w_im*z_im) + j(w_im*z_re + w_re*z_im)
  // the real part
  simde__m128i xy_re_128 = simde_mm_madd_epi16(oai_mm_conj(input_x), input_y); //Re: (x_re*y_re - x_im*y_im)
  simde__m128i wz_re_128 = simde_mm_madd_epi16(oai_mm_conj(input_w), input_z); //Re: (w_re*z_re - w_im*z_im)
  xy_re_128 = simde_mm_sub_epi32(xy_re_128, wz_re_128);

  // the imag part
  simde__m128i xy_im_128 = simde_mm_madd_epi16(oai_mm_swap(input_x), input_y); //Im: (x_im*y_re + x_re*y_im)
  simde__m128i wz_im_128 = simde_mm_madd_epi16(oai_mm_swap(input_w), input_z); //Im: (w_im*z_re + w_re*z_im)
  xy_im_128 = simde_mm_sub_epi32(xy_im_128, wz_im_128);

  //print_ints("rx_re:",(int32_t*)&xy_re_128[0]);
  //print_ints("rx_Img:",(int32_t*)&xy_im_128[0]);
  //divide by matrix det and convert back to Q15 before packing
  uint64_t sum_det = 0;
  for (int k = 0; k < 4; k++) {
    sum_det += (((uint32_t *)&det)[k]);
  }
  // Add bias to reduce rounding error
  sum_det = (sum_det + 2) >> 2;

  int b = log2_approx(sum_det) - 8;
  if (b > 0) {
    xy_re_128 = simde_mm_srai_epi32(xy_re_128, b);
    xy_im_128 = simde_mm_srai_epi32(xy_im_128, b);
  } else {
    xy_re_128 = simde_mm_slli_epi32(xy_re_128, -b);
    xy_im_128 = simde_mm_slli_epi32(xy_im_128, -b);
  }

  simde__m128i output = oai_mm_pack(xy_re_128, xy_im_128);

  return(output);
}

/* Zero Forcing Rx function: nr_construct_HhH_elements()
 *
 *
 * */
static void nr_ulsch_construct_HhH_elements(c16_t *conjch00_ch00,
                                            c16_t *conjch01_ch01,
                                            c16_t *conjch11_ch11,
                                            c16_t *conjch10_ch10, //
                                            c16_t *conjch20_ch20,
                                            c16_t *conjch21_ch21,
                                            c16_t *conjch30_ch30,
                                            c16_t *conjch31_ch31,
                                            c16_t *conjch00_ch01, // 00_01
                                            c16_t *conjch01_ch00, // 01_00
                                            c16_t *conjch10_ch11, // 10_11
                                            c16_t *conjch11_ch10, // 11_10
                                            c16_t *conjch20_ch21,
                                            c16_t *conjch21_ch20,
                                            c16_t *conjch30_ch31,
                                            c16_t *conjch31_ch30,
                                            c16_t *after_mf_00,
                                            c16_t *after_mf_01,
                                            c16_t *after_mf_10,
                                            c16_t *after_mf_11,
                                            unsigned short nb_rb,
                                            unsigned char symbol)
{
  //This function is used to construct the (H_hermitian * H matrix) matrix elements
  simde__m128i *conjch00_ch00_128 = (simde__m128i *)conjch00_ch00;
  simde__m128i *conjch01_ch01_128 = (simde__m128i *)conjch01_ch01;
  simde__m128i *conjch11_ch11_128 = (simde__m128i *)conjch11_ch11;
  simde__m128i *conjch10_ch10_128 = (simde__m128i *)conjch10_ch10;

  simde__m128i *conjch20_ch20_128 = (simde__m128i *)conjch20_ch20;
  simde__m128i *conjch21_ch21_128 = (simde__m128i *)conjch21_ch21;
  simde__m128i *conjch30_ch30_128 = (simde__m128i *)conjch30_ch30;
  simde__m128i *conjch31_ch31_128 = (simde__m128i *)conjch31_ch31;

  simde__m128i *conjch00_ch01_128 = (simde__m128i *)conjch00_ch01;
  simde__m128i *conjch01_ch00_128 = (simde__m128i *)conjch01_ch00;
  simde__m128i *conjch10_ch11_128 = (simde__m128i *)conjch10_ch11;
  simde__m128i *conjch11_ch10_128 = (simde__m128i *)conjch11_ch10;

  simde__m128i *conjch20_ch21_128 = (simde__m128i *)conjch20_ch21;
  simde__m128i *conjch21_ch20_128 = (simde__m128i *)conjch21_ch20;
  simde__m128i *conjch30_ch31_128 = (simde__m128i *)conjch30_ch31;
  simde__m128i *conjch31_ch30_128 = (simde__m128i *)conjch31_ch30;

  simde__m128i *after_mf_00_128 = (simde__m128i *)after_mf_00;
  simde__m128i *after_mf_01_128 = (simde__m128i *)after_mf_01;
  simde__m128i *after_mf_10_128 = (simde__m128i *)after_mf_10;
  simde__m128i *after_mf_11_128 = (simde__m128i *)after_mf_11;

  for (unsigned short rb=0; rb<3*nb_rb; rb++) {

    after_mf_00_128[0] = simde_mm_adds_epi16(conjch00_ch00_128[0], conjch10_ch10_128[0]); //00_00 + 10_10
    if (conjch20_ch20 != NULL) after_mf_00_128[0] = simde_mm_adds_epi16(after_mf_00_128[0], conjch20_ch20_128[0]);
    if (conjch30_ch30 != NULL) after_mf_00_128[0] = simde_mm_adds_epi16(after_mf_00_128[0], conjch30_ch30_128[0]);

    after_mf_11_128[0] = simde_mm_adds_epi16(conjch01_ch01_128[0], conjch11_ch11_128[0]); //01_01 + 11_11
    if (conjch21_ch21 != NULL) after_mf_11_128[0] = simde_mm_adds_epi16(after_mf_11_128[0], conjch21_ch21_128[0]);
    if (conjch31_ch31 != NULL) after_mf_11_128[0] = simde_mm_adds_epi16(after_mf_11_128[0], conjch31_ch31_128[0]);

    after_mf_01_128[0] = simde_mm_adds_epi16(conjch00_ch01_128[0], conjch10_ch11_128[0]); //00_01 + 10_11
    if (conjch20_ch21 != NULL) after_mf_01_128[0] = simde_mm_adds_epi16(after_mf_01_128[0], conjch20_ch21_128[0]);
    if (conjch30_ch31 != NULL) after_mf_01_128[0] = simde_mm_adds_epi16(after_mf_01_128[0], conjch30_ch31_128[0]);

    after_mf_10_128[0] = simde_mm_adds_epi16(conjch01_ch00_128[0], conjch11_ch10_128[0]); //01_00 + 11_10
    if (conjch21_ch20 != NULL) after_mf_10_128[0] = simde_mm_adds_epi16(after_mf_10_128[0], conjch21_ch20_128[0]);
    if (conjch31_ch30 != NULL) after_mf_10_128[0] = simde_mm_adds_epi16(after_mf_10_128[0], conjch31_ch30_128[0]);

#ifdef DEBUG_DLSCH_DEMOD
    if ((rb<=30))
    {
      printf(" \n construct_HhH_elements \n");
      print_shorts("after_mf_00_128:",(int16_t*)&after_mf_00_128[0]);
      print_shorts("after_mf_01_128:",(int16_t*)&after_mf_01_128[0]);
      print_shorts("after_mf_10_128:",(int16_t*)&after_mf_10_128[0]);
      print_shorts("after_mf_11_128:",(int16_t*)&after_mf_11_128[0]);
    }
#endif
    conjch00_ch00_128+=1;
    conjch10_ch10_128+=1;
    conjch01_ch01_128+=1;
    conjch11_ch11_128+=1;

    if (conjch20_ch20 != NULL) conjch20_ch20_128+=1;
    if (conjch21_ch21 != NULL) conjch21_ch21_128+=1;
    if (conjch30_ch30 != NULL) conjch30_ch30_128+=1;
    if (conjch31_ch31 != NULL) conjch31_ch31_128+=1;

    conjch00_ch01_128+=1;
    conjch01_ch00_128+=1;
    conjch10_ch11_128+=1;
    conjch11_ch10_128+=1;

    if (conjch20_ch21 != NULL) conjch20_ch21_128+=1;
    if (conjch21_ch20 != NULL) conjch21_ch20_128+=1;
    if (conjch30_ch31 != NULL) conjch30_ch31_128+=1;
    if (conjch31_ch30 != NULL) conjch31_ch30_128+=1;

    after_mf_00_128 += 1;
    after_mf_01_128 += 1;
    after_mf_10_128 += 1;
    after_mf_11_128 += 1;
  }
}

// MMSE Rx function: nr_ulsch_mmse_2layers()
static uint8_t nr_ulsch_mmse_2layers(int **rxdataF_comp,
                                     uint32_t buffer_length,
                                     int nb_rx_ant,
                                     c16_t ul_ch_mag[][buffer_length],
                                     c16_t ul_ch_magb[][buffer_length],
                                     c16_t ul_ch_magc[][buffer_length],
                                     c16_t ul_ch_estimates_ext[][nb_rx_ant][buffer_length],
                                     unsigned short nb_rb,
                                     unsigned char mod_order,
                                     int shift,
                                     unsigned char symbol,
                                     int length,
                                     uint32_t noise_var)
{
  uint32_t nb_rb_0 = length/12 + ((length%12)?1:0);

  /* we need at least alignment to 16 bytes, let's put 32 to be sure
   * (maybe not necessary but doesn't hurt)
   */
  c16_t conjch00_ch01[12 * nb_rb] __attribute__((aligned(32)));
  c16_t conjch01_ch00[12 * nb_rb] __attribute__((aligned(32)));
  c16_t conjch10_ch11[12 * nb_rb] __attribute__((aligned(32)));
  c16_t conjch11_ch10[12 * nb_rb] __attribute__((aligned(32)));
  c16_t conjch00_ch00[12 * nb_rb] __attribute__((aligned(32)));
  c16_t conjch01_ch01[12 * nb_rb] __attribute__((aligned(32)));
  c16_t conjch10_ch10[12 * nb_rb] __attribute__((aligned(32)));
  c16_t conjch11_ch11[12 * nb_rb] __attribute__((aligned(32)));
  c16_t conjch20_ch20[12 * nb_rb] __attribute__((aligned(32)));
  c16_t conjch21_ch21[12 * nb_rb] __attribute__((aligned(32)));
  c16_t conjch30_ch30[12 * nb_rb] __attribute__((aligned(32)));
  c16_t conjch31_ch31[12 * nb_rb] __attribute__((aligned(32)));
  c16_t conjch20_ch21[12 * nb_rb] __attribute__((aligned(32)));
  c16_t conjch30_ch31[12 * nb_rb] __attribute__((aligned(32)));
  c16_t conjch21_ch20[12 * nb_rb] __attribute__((aligned(32)));
  c16_t conjch31_ch30[12 * nb_rb] __attribute__((aligned(32)));

  c16_t af_mf_00[12 * nb_rb] __attribute__((aligned(32)));
  c16_t af_mf_01[12 * nb_rb] __attribute__((aligned(32)));
  c16_t af_mf_10[12 * nb_rb] __attribute__((aligned(32)));
  c16_t af_mf_11[12 * nb_rb] __attribute__((aligned(32)));
  uint32_t determ_fin[12*nb_rb] __attribute__((aligned(32)));

  c16_t *ch00, *ch01, *ch10, *ch11;
  c16_t *ch20, *ch30, *ch21, *ch31;
  switch (nb_rx_ant) {
    case 2://
      ch00 = ul_ch_estimates_ext[0][0];
      ch01 = ul_ch_estimates_ext[1][0];
      ch10 = ul_ch_estimates_ext[0][1];
      ch11 = ul_ch_estimates_ext[1][1];
      ch20 = NULL;
      ch21 = NULL;
      ch30 = NULL;
      ch31 = NULL;
      break;

    case 4://
      ch00 = ul_ch_estimates_ext[0][0];
      ch01 = ul_ch_estimates_ext[1][0];
      ch10 = ul_ch_estimates_ext[0][1];
      ch11 = ul_ch_estimates_ext[1][1];
      ch20 = ul_ch_estimates_ext[0][2];
      ch21 = ul_ch_estimates_ext[1][2];
      ch30 = ul_ch_estimates_ext[0][3];
      ch31 = ul_ch_estimates_ext[1][3];
      break;

    default:
      return -1;
      break;
  }

  /* 1- Compute the rx channel matrix after compensation: (1/2^log2_max)x(H_herm x H)
   * for n_rx = 2
   * |conj_H_00       conj_H_10|    | H_00         H_01|   |(conj_H_00xH_00+conj_H_10xH_10)   (conj_H_00xH_01+conj_H_10xH_11)|
   * |                         |  x |                  | = |                                                                 |
   * |conj_H_01       conj_H_11|    | H_10         H_11|   |(conj_H_01xH_00+conj_H_11xH_10)   (conj_H_01xH_01+conj_H_11xH_11)|
   *
   */

  if (nb_rx_ant >= 2) {
    // (1/2^log2_maxh)*conj_H_00xH_00: (1/(64*2))conjH_00*H_00*2^15
    nr_ulsch_conjch0_mult_ch1(ch00,
                        ch00,
                        conjch00_ch00,
                        nb_rb_0,
                        shift);
    // (1/2^log2_maxh)*conj_H_10xH_10: (1/(64*2))conjH_10*H_10*2^15
    nr_ulsch_conjch0_mult_ch1(ch10,
                        ch10,
                        conjch10_ch10,
                        nb_rb_0,
                        shift);
    // conj_H_00xH_01
    nr_ulsch_conjch0_mult_ch1(ch00,
                        ch01,
                        conjch00_ch01,
                        nb_rb_0,
                        shift); // this shift is equal to the channel level log2_maxh
    // conj_H_10xH_11
    nr_ulsch_conjch0_mult_ch1(ch10,
                        ch11,
                        conjch10_ch11,
                        nb_rb_0,
                        shift);
    // conj_H_01xH_01
    nr_ulsch_conjch0_mult_ch1(ch01,
                        ch01,
                        conjch01_ch01,
                        nb_rb_0,
                        shift);
    // conj_H_11xH_11
    nr_ulsch_conjch0_mult_ch1(ch11,
                        ch11,
                        conjch11_ch11,
                        nb_rb_0,
                        shift);
    // conj_H_01xH_00
    nr_ulsch_conjch0_mult_ch1(ch01,
                        ch00,
                        conjch01_ch00,
                        nb_rb_0,
                        shift);
    // conj_H_11xH_10
    nr_ulsch_conjch0_mult_ch1(ch11,
                        ch10,
                        conjch11_ch10,
                        nb_rb_0,
                        shift);
  }
  if (nb_rx_ant == 4) {
    // (1/2^log2_maxh)*conj_H_20xH_20: (1/(64*2*16))conjH_20*H_20*2^15
    nr_ulsch_conjch0_mult_ch1(ch20,
                        ch20,
                        conjch20_ch20,
                        nb_rb_0,
                        shift);

    // (1/2^log2_maxh)*conj_H_30xH_30: (1/(64*2*4))conjH_30*H_30*2^15
    nr_ulsch_conjch0_mult_ch1(ch30,
                        ch30,
                        conjch30_ch30,
                        nb_rb_0,
                        shift);

    // (1/2^log2_maxh)*conj_H_20xH_20: (1/(64*2))conjH_20*H_20*2^15
    nr_ulsch_conjch0_mult_ch1(ch20,
                        ch21,
                        conjch20_ch21,
                        nb_rb_0,
                        shift);

    nr_ulsch_conjch0_mult_ch1(ch30,
                        ch31,
                        conjch30_ch31,
                        nb_rb_0,
                        shift);

    nr_ulsch_conjch0_mult_ch1(ch21,
                        ch21,
                        conjch21_ch21,
                        nb_rb_0,
                        shift);

    nr_ulsch_conjch0_mult_ch1(ch31,
                        ch31,
                        conjch31_ch31,
                        nb_rb_0,
                        shift);

    // (1/2^log2_maxh)*conj_H_20xH_20: (1/(64*2))conjH_20*H_20*2^15
    nr_ulsch_conjch0_mult_ch1(ch21,
                        ch20,
                        conjch21_ch20,
                        nb_rb_0,
                        shift);

    nr_ulsch_conjch0_mult_ch1(ch31,
                        ch30,
                        conjch31_ch30,
                        nb_rb_0,
                        shift);

    nr_ulsch_construct_HhH_elements(conjch00_ch00,
                              conjch01_ch01,
                              conjch11_ch11,
                              conjch10_ch10,//
                              conjch20_ch20,
                              conjch21_ch21,
                              conjch30_ch30,
                              conjch31_ch31,
                              conjch00_ch01,
                              conjch01_ch00,
                              conjch10_ch11,
                              conjch11_ch10,//
                              conjch20_ch21,
                              conjch21_ch20,
                              conjch30_ch31,
                              conjch31_ch30,
                              af_mf_00,
                              af_mf_01,
                              af_mf_10,
                              af_mf_11,
                              nb_rb_0,
                              symbol);
  }
  if (nb_rx_ant == 2) {
    nr_ulsch_construct_HhH_elements(conjch00_ch00,
                              conjch01_ch01,
                              conjch11_ch11,
                              conjch10_ch10,//
                              NULL,
                              NULL,
                              NULL,
                              NULL,
                              conjch00_ch01,
                              conjch01_ch00,
                              conjch10_ch11,
                              conjch11_ch10,//
                              NULL,
                              NULL,
                              NULL,
                              NULL,
                              af_mf_00,
                              af_mf_01,
                              af_mf_10,
                              af_mf_11,
                              nb_rb_0,
                              symbol);
  }

  // Add noise_var such that: H^h * H + noise_var * I
  if (noise_var != 0) {
    simde__m128i nvar_128i = simde_mm_set1_epi32(noise_var);
    simde__m128i *af_mf_00_128i = (simde__m128i *)af_mf_00;
    simde__m128i *af_mf_11_128i = (simde__m128i *)af_mf_11;
    for (int k = 0; k < 3 * nb_rb_0; k++) {
      af_mf_00_128i[0] = simde_mm_add_epi32(af_mf_00_128i[0], nvar_128i);
      af_mf_11_128i[0] = simde_mm_add_epi32(af_mf_11_128i[0], nvar_128i);
      af_mf_00_128i++;
      af_mf_11_128i++;
    }
  }

  //det_HhH = ad -bc
  nr_ulsch_det_HhH(af_mf_00,//a
             af_mf_01,//b
             af_mf_10,//c
             af_mf_11,//d
             determ_fin,
             nb_rb_0,
             symbol,
             shift);
  /* 2- Compute the channel matrix inversion **********************************
   *
     *    |(conj_H_00xH_00+conj_H_10xH_10)   (conj_H_00xH_01+conj_H_10xH_11)|
     * A= |                                                                 |
     *    |(conj_H_01xH_00+conj_H_11xH_10)   (conj_H_01xH_01+conj_H_11xH_11)|
     *
     *
     *
     *inv(A) =(1/det)*[d  -b
     *                 -c  a]
     *
     *
     **************************************************************************/
  simde__m128i *ul_ch_mag128_0 = NULL, *ul_ch_mag128b_0 = NULL, *ul_ch_mag128c_0 = NULL; // Layer 0
  simde__m128i *ul_ch_mag128_1 = NULL, *ul_ch_mag128b_1 = NULL, *ul_ch_mag128c_1 = NULL; // Layer 1
  simde__m128i mmtmpD0, mmtmpD1, mmtmpD2, mmtmpD3;
  simde__m128i QAM_amp128 = {0}, QAM_amp128b = {0}, QAM_amp128c = {0};

  simde__m128i *determ_fin_128 = (simde__m128i *)&determ_fin[0];

  simde__m128i *after_mf_a_128 = (simde__m128i *)af_mf_00;
  simde__m128i *after_mf_b_128 = (simde__m128i *)af_mf_01;
  simde__m128i *after_mf_c_128 = (simde__m128i *)af_mf_10;
  simde__m128i *after_mf_d_128 = (simde__m128i *)af_mf_11;
  
  simde__m128i *rxdataF_comp128_0 = (simde__m128i *)&rxdataF_comp[0][symbol * buffer_length];
  simde__m128i *rxdataF_comp128_1 = (simde__m128i *)&rxdataF_comp[nb_rx_ant][symbol * buffer_length];

  if (mod_order > 2) {
    if (mod_order == 4) {
      QAM_amp128 = simde_mm_set1_epi16(QAM16_n1); // 2/sqrt(10)
      QAM_amp128b = simde_mm_setzero_si128();
      QAM_amp128c = simde_mm_setzero_si128();
    } else if (mod_order == 6) {
      QAM_amp128 = simde_mm_set1_epi16(QAM64_n1); // 4/sqrt{42}
      QAM_amp128b = simde_mm_set1_epi16(QAM64_n2); // 2/sqrt{42}
      QAM_amp128c = simde_mm_setzero_si128();
    } else if (mod_order == 8) {
      QAM_amp128 =  simde_mm_set1_epi16(QAM256_n1);
      QAM_amp128b = simde_mm_set1_epi16(QAM256_n2);
      QAM_amp128c = simde_mm_set1_epi16(QAM256_n3);
    }
    ul_ch_mag128_0 = (simde__m128i *)&ul_ch_mag[0];
    ul_ch_mag128b_0 = (simde__m128i *)&ul_ch_magb[0];
    ul_ch_mag128c_0 = (simde__m128i *)&ul_ch_magc[0];
    ul_ch_mag128_1 = (simde__m128i *)&ul_ch_mag[1];
    ul_ch_mag128b_1 = (simde__m128i *)&ul_ch_magb[1];
    ul_ch_mag128c_1 = (simde__m128i *)&ul_ch_magc[1];
  }

  for (int rb = 0; rb < 3 * nb_rb_0; rb++) {

    // Magnitude computation
    if (mod_order > 2) {
      uint64_t sum_det = 0;
      for (int k = 0; k < 4; k++) {
        sum_det += (((uint32_t *)&determ_fin_128[0])[k]);
      }
      // Add bias to reduce rounding error
      sum_det = (sum_det + 2) >> 2;

      int b = log2_approx(sum_det) - 8;
      if (b > 0) {
        mmtmpD2 = simde_mm_srai_epi32(determ_fin_128[0], b);
      } else {
        mmtmpD2 = simde_mm_slli_epi32(determ_fin_128[0], -b);
      }
      mmtmpD3 = simde_mm_unpacklo_epi32(mmtmpD2, mmtmpD2);
      mmtmpD2 = simde_mm_unpackhi_epi32(mmtmpD2, mmtmpD2);
      mmtmpD2 = simde_mm_packs_epi32(mmtmpD3, mmtmpD2);

      // Layer 0
      ul_ch_mag128_0[0] = mmtmpD2;
      ul_ch_mag128b_0[0] = mmtmpD2;
      ul_ch_mag128c_0[0] = mmtmpD2;
      ul_ch_mag128_0[0] = simde_mm_mulhi_epi16(ul_ch_mag128_0[0], QAM_amp128);
      ul_ch_mag128_0[0] = simde_mm_slli_epi16(ul_ch_mag128_0[0], 1);
      ul_ch_mag128b_0[0] = simde_mm_mulhi_epi16(ul_ch_mag128b_0[0], QAM_amp128b);
      ul_ch_mag128b_0[0] = simde_mm_slli_epi16(ul_ch_mag128b_0[0], 1);
      ul_ch_mag128c_0[0] = simde_mm_mulhi_epi16(ul_ch_mag128c_0[0], QAM_amp128c);
      ul_ch_mag128c_0[0] = simde_mm_slli_epi16(ul_ch_mag128c_0[0], 1);

      // Layer 1
      ul_ch_mag128_1[0] = mmtmpD2;
      ul_ch_mag128b_1[0] = mmtmpD2;
      ul_ch_mag128c_1[0] = mmtmpD2;
      ul_ch_mag128_1[0] = simde_mm_mulhi_epi16(ul_ch_mag128_1[0], QAM_amp128);
      ul_ch_mag128_1[0] = simde_mm_slli_epi16(ul_ch_mag128_1[0], 1);
      ul_ch_mag128b_1[0] = simde_mm_mulhi_epi16(ul_ch_mag128b_1[0], QAM_amp128b);
      ul_ch_mag128b_1[0] = simde_mm_slli_epi16(ul_ch_mag128b_1[0], 1);
      ul_ch_mag128c_1[0] = simde_mm_mulhi_epi16(ul_ch_mag128c_1[0], QAM_amp128c);
      ul_ch_mag128c_1[0] = simde_mm_slli_epi16(ul_ch_mag128c_1[0], 1);
    }

    // multiply by channel Inv
    //rxdataF_zf128_0 = rxdataF_comp128_0*d - b*rxdataF_comp128_1
    //rxdataF_zf128_1 = rxdataF_comp128_1*a - c*rxdataF_comp128_0
    //printf("layer_1 \n");
    mmtmpD0 = nr_ulsch_comp_muli_sum(rxdataF_comp128_0[0],
                               after_mf_d_128[0],
                               rxdataF_comp128_1[0],
                               after_mf_b_128[0],
                               determ_fin_128[0]);

    //printf("layer_2 \n");
    mmtmpD1 = nr_ulsch_comp_muli_sum(rxdataF_comp128_1[0],
                               after_mf_a_128[0],
                               rxdataF_comp128_0[0],
                               after_mf_c_128[0],
                               determ_fin_128[0]);

    rxdataF_comp128_0[0] = mmtmpD0;
    rxdataF_comp128_1[0] = mmtmpD1;

#ifdef DEBUG_DLSCH_DEMOD
    printf("\n Rx signal after ZF l%d rb%d\n",symbol,rb);
    print_shorts(" Rx layer 1:",(int16_t*)&rxdataF_comp128_0[0]);
    print_shorts(" Rx layer 2:",(int16_t*)&rxdataF_comp128_1[0]);
#endif
    determ_fin_128 += 1;
    ul_ch_mag128_0 += 1;
    ul_ch_mag128_1 += 1;
    ul_ch_mag128b_0 += 1;
    ul_ch_mag128b_1 += 1;
    ul_ch_mag128c_0 += 1;
    ul_ch_mag128c_1 += 1;
    rxdataF_comp128_0 += 1;
    rxdataF_comp128_1 += 1;
    after_mf_a_128 += 1;
    after_mf_b_128 += 1;
    after_mf_c_128 += 1;
    after_mf_d_128 += 1;
  }
   return(0);
}

static void inner_rx(PHY_VARS_gNB *gNB,
                     int ulsch_id,
                     uint32_t frame,
                     int slot,
                     NR_DL_FRAME_PARMS *frame_parms,
                     NR_gNB_PUSCH *pusch_vars,
                     nfapi_nr_pusch_pdu_t *rel15_ul,
                     c16_t **rxF,
                     c16_t **ul_ch,
                     int16_t **llr,
                     int soffset,
                     int length,
                     int symbol,
                     int output_shift,
                     uint32_t nvar,
                     c16_t *rxFext_slot,
                     c16_t *chFext_slot)
{
  int nb_layer = rel15_ul->nrOfLayers;
  int nb_rx_ant = frame_parms->nb_antennas_rx;
  int dmrs_symbol_flag = (rel15_ul->ul_dmrs_symb_pos >> symbol) & 0x01;
  int buffer_length = ceil_mod(rel15_ul->rb_size * NR_NB_SC_PER_RB, 16);
  c16_t rxFext[nb_rx_ant][buffer_length] __attribute__((aligned(32)));
  c16_t chFext[nb_layer][nb_rx_ant][buffer_length] __attribute__((aligned(32)));

  memset(rxFext, 0, sizeof(rxFext));
  memset(chFext, 0, sizeof(chFext));
  int dmrs_symbol;
  if (gNB->chest_time == 0)
    dmrs_symbol = dmrs_symbol_flag ? symbol : get_valid_dmrs_idx_for_channel_est(rel15_ul->ul_dmrs_symb_pos, symbol);
  else { // average of channel estimates stored in first symbol
    int end_symbol = rel15_ul->start_symbol_index + rel15_ul->nr_of_symbols;
    dmrs_symbol = get_next_dmrs_symbol_in_slot(rel15_ul->ul_dmrs_symb_pos, rel15_ul->start_symbol_index, end_symbol);
  }

  for (int aarx = 0; aarx < nb_rx_ant; aarx++) {
    for (int aatx = 0; aatx < nb_layer; aatx++) {
      nr_ulsch_extract_rbs(rxF[aarx],
                           (c16_t *)pusch_vars->ul_ch_estimates[aatx * nb_rx_ant + aarx],
                           rxFext[aarx],
                           chFext[aatx][aarx],
                           soffset+(symbol * frame_parms->ofdm_symbol_size),
                           dmrs_symbol * frame_parms->ofdm_symbol_size,
                           aarx,
                           dmrs_symbol_flag, 
                           rel15_ul,
                           frame_parms);
#if T_TRACER
      int nb_re_pusch = NR_NB_SC_PER_RB * rel15_ul->rb_size;
      // Assume assume Tx and Rx = 1
      if (T_ACTIVE(T_GNB_PHY_UL_FD_PUSCH_IQ)) {
        copy_c16_data_to_slot_memory(rxFext[aarx], rxFext_slot, nb_re_pusch, symbol);
      }
      if (T_ACTIVE(T_GNB_PHY_UL_FD_CHAN_EST_DMRS_INTERPL)) {
        copy_c16_data_to_slot_memory(chFext[aatx][aarx], chFext_slot, nb_re_pusch, symbol);
      }
#endif
    }
  }
  c16_t rho[nb_layer][nb_layer][buffer_length] __attribute__((aligned(32)));
  c16_t rxF_ch_maga  [nb_layer][buffer_length] __attribute__((aligned(32)));
  c16_t rxF_ch_magb  [nb_layer][buffer_length] __attribute__((aligned(32)));
  c16_t rxF_ch_magc  [nb_layer][buffer_length] __attribute__((aligned(32)));

  memset(rho, 0, sizeof(rho));
  memset(rxF_ch_maga, 0, sizeof(rxF_ch_maga));
  memset(rxF_ch_magb, 0, sizeof(rxF_ch_magb));
  memset(rxF_ch_magc, 0, sizeof(rxF_ch_magc));
  for (int i = 0; i < nb_layer; i++)
    memset(&pusch_vars->rxdataF_comp[i*nb_rx_ant][symbol * buffer_length], 0, sizeof(int32_t) * buffer_length);

  // Phase-4 UL MU-MIMO joint MMSE-IRC (env OAI_UL_MU_IRC). If this connected 1-layer UE is
  // co-scheduled with a partner on the same PRBs (same rb_start/rb_size), run a 2-stream receiver
  // [layer0=self, layer1=partner] to NULL the partner and decode self. Reuses the existing 2-layer
  // channel-compensation + nr_ulsch_mmse_2layers; local 2-layer buffers; writes only this UE's
  // llr[0] on data symbols. DMRS symbols fall through to the normal path. Off => unchanged.
  {
    static int mu_irc = -1;
    if (mu_irc < 0) { const char *e = getenv("OAI_UL_MU_IRC"); mu_irc = (e && e[0]) ? 1 : 0; }
    // MU regime gate (set by the MAC scheduler): only true when >=2 UEs are fully connected and
    // nobody is in RA. Without this, full-band Msg3 (rb 0+273) passes the rb_size filter below and
    // IRC corrupts RA -> endless PRACH-retry storm. This is the scheduler->PHY co-sched marker.
    extern volatile int g_mu_mimo_active;
    int partner = -1;
    // Only fire on genuine co-scheduled DATA: MU regime active, large allocation, distinct rnti,
    // matching PRBs. Prevents the IRC mis-pairing during attach that breaks decode.
    // nb_rx_ant >= 2 REQUIRED: the 2-layer joint detector is degenerate at 1 antenna (can't
    // separate 2 co-channel UEs with 1 RX) -> it corrupts EVERY decode (grants carry garbage,
    // both UEs starve). At 1 antenna the co-scheduled UEs simply collide; IRC cannot help.
    { static int dg = 0; if (mu_irc && !dmrs_symbol_flag && rel15_ul->rb_size > 137 && dg++ < 6)
        LOG_E(PHY, "[MU GATE] nb_rx_ant=%d g_mu=%d nb_layer=%d rb_size=%d max_pusch=%d\n",
              nb_rx_ant, g_mu_mimo_active, nb_layer, rel15_ul->rb_size, gNB->max_nb_pusch); }
    if (mu_irc && g_mu_mimo_active && pusch_vars->log2_maxh > 0 && nb_rx_ant >= 2 && nb_layer == 1 && !dmrs_symbol_flag && rel15_ul->rb_size > 137) {
      // CRASH FIX (was Block-1): gNB->ulsch[id].harq_process is NULL for unused slots -> the old
      // unguarded ->ulsch_pdu deref segfaulted (at 0) the first time the scan ran past the active
      // ids (dmesg: Tpool segfault at 0, du.log dead right after first [MU METRIC] in EVERY run —
      // the "instability/churn" was the DU dying). Guard active+harq_process, and require the
      // partner to be scheduled THIS SAME frame/slot (kills the stale-partner mispairing too).
      const NR_gNB_ULSCH_t *cur = &gNB->ulsch[ulsch_id];
      for (int id = 0; id < gNB->max_nb_pusch; id++) {
        if (id == ulsch_id) continue;
        const NR_gNB_ULSCH_t *u = &gNB->ulsch[id];
        if (!u->active || u->harq_process == NULL) continue;
        if (u->frame != cur->frame || u->slot != cur->slot) continue; // co-scheduled this slot only
        const nfapi_nr_pusch_pdu_t *p = &u->harq_process->ulsch_pdu;
        if (p->rb_size == rel15_ul->rb_size && p->rb_start == rel15_ul->rb_start && p->rnti != rel15_ul->rnti) {
          partner = id;
          break;
        }
      }
    }
    // Coverage counters: a TB whose data symbols are only PARTLY IRC'd dies — any fall-through
    // symbol is decoded by plain MRC WITH co-channel interference => garbage LLRs for those REs.
    // Track why symbols fall through; printed with [MU METRIC].
    static long mu_c_irc = 0, mu_c_nopart = 0, mu_c_rxe = 0, mu_c_parte = 0;
    if (mu_irc && g_mu_mimo_active && pusch_vars->log2_maxh > 0 && nb_rx_ant >= 2 && nb_layer == 1 && !dmrs_symbol_flag && rel15_ul->rb_size > 137 && partner < 0)
      mu_c_nopart++;
    // Signal-present guard: only run IRC when there's actually a received signal to separate on
    // this symbol (rxFext non-trivial). Prevents firing on empty/phantom-grant symbols where the
    // UE isn't transmitting (rxFext~0), which corrupted decode and broke attach.
    if (partner >= 0) {
      long rxe = 0;
      for (int a = 0; a < nb_rx_ant; a++)
        for (int i = 0; i < 32 && i < buffer_length; i++) rxe += abs(rxFext[a][i].r) + abs(rxFext[a][i].i);
      if (rxe < 8) { partner = -1; mu_c_rxe++; } // no real signal this symbol -> fall through
    }
    if (partner >= 0) {
      NR_gNB_PUSCH *pv_p = &gNB->pusch_vars[partner];
      c16_t chF2[2][nb_rx_ant][buffer_length] __attribute__((aligned(32)));
      c16_t dummy[buffer_length] __attribute__((aligned(32)));
      memset(chF2, 0, sizeof(chF2));
      // REUSE the normal extraction: rxFext (received) + chFext[0] (self channel) are already
      // filled above. Only extract the PARTNER channel into chF2[1]. chF2[0] = self.
      for (int aarx = 0; aarx < nb_rx_ant; aarx++) {
        memcpy(chF2[0][aarx], chFext[0][aarx], buffer_length * sizeof(c16_t));
        nr_ulsch_extract_rbs(rxF[aarx], (c16_t *)pv_p->ul_ch_estimates[aarx], dummy, chF2[1][aarx],
                             soffset + (symbol * frame_parms->ofdm_symbol_size), dmrs_symbol * frame_parms->ofdm_symbol_size,
                             aarx, dmrs_symbol_flag, rel15_ul, frame_parms);
      }
      { // diagnostic: is the (normal) self channel estimate non-zero here?
        static int cd = 0;
        if (cd++ < 4) {
          long s0 = 0, r0 = 0;
          for (int a = 0; a < nb_rx_ant; a++) { s0 += abs(chFext[0][a][10].r) + abs(chFext[0][a][10].i); r0 += abs(rxFext[a][10].r) + abs(rxFext[a][10].i); }
          LOG_E(PHY, "[MU DIAG] normal-path |chFext[0]@10|=%ld |rxFext@10|=%ld (non-zero => estimate ok)\n", s0, r0);
        }
      }
      // STAGE-1 GENIE (OAI_UL_MU_GENIE): replace the ESTIMATED channels with the KNOWN orthogonal
      // vrtsim steering beams (DFT: order k -> h[a]=exp(j2*pi*a*k/nb_rx)), assigned by connection
      // order. Isolates the receiver from DMRS estimation: if both UEs decode with genie channels
      // then the chain (co-sched / 2 antennas / IRC math) is PROVEN and the ONLY remaining problem
      // is pilot estimation. corr2pct should read ~0. Scale to the estimated per-RE magnitude so the
      // fixed-point MMSE stays in range. (If both CRC-fail but corr~0, the self/partner beam
      // assignment is swapped vs vrtsim -> try OAI_UL_MU_GENIE=2 to swap.)
      {
        static int genie = -1;
        if (genie < 0) { const char *e = getenv("OAI_UL_MU_GENIE"); genie = (e && e[0]) ? atoi(e) : 0; }
        if (genie) {
          static rnti_t gord[8] = {0}; static int gcnt = 0;
          rnti_t prnti = gNB->ulsch[partner].harq_process->ulsch_pdu.rnti;
          int so = -1, po = -1;
          for (int i = 0; i < gcnt; i++) { if (gord[i] == rel15_ul->rnti) so = i; if (gord[i] == prnti) po = i; }
          if (so < 0 && gcnt < 8) { so = gcnt; gord[gcnt++] = rel15_ul->rnti; }
          if (po < 0 && gcnt < 8) { po = gcnt; gord[gcnt++] = prnti; }
          if (so < 0) so = 0;
          if (po < 0) po = 1;
          if (genie == 2) { int t = so; so = po; po = t; }  // swap assignment
          // Reference amplitude = mean per-antenna |chFext[0]| over the band, so the genie channel
          // magnitude MATCHES the true gain (and log2_maxh, computed from the estimate) -> MMSE well
          // scaled. Per-RE |chFext| is noisy/small at many REs (gave chSelf=22, tiny LLR); a band
          // mean is stable and correctly scaled.
          long asum = 0; int an = 0;
          for (int rr = 0; rr < buffer_length; rr += 12)
            for (int a = 0; a < nb_rx_ant; a++) { asum += abs(chFext[0][a][rr].r) + abs(chFext[0][a][rr].i); an++; }
          int gamp = (an > 0) ? (int)(asum / an) : 200; if (gamp < 16) gamp = 200;
          for (int re = 0; re < buffer_length; re++) {
            int amp = gamp;
            for (int a = 0; a < nb_rx_ant; a++) {
              double ps = 2.0 * M_PI * a * so / (double)nb_rx_ant, pp = 2.0 * M_PI * a * po / (double)nb_rx_ant;
              chF2[0][a][re].r = (int16_t)lround(cos(ps) * amp); chF2[0][a][re].i = (int16_t)lround(sin(ps) * amp);
              chF2[1][a][re].r = (int16_t)lround(cos(pp) * amp); chF2[1][a][re].i = (int16_t)lround(sin(pp) * amp);
            }
          }
        }
      }
      // Partner-channel guard (Defect-2 fix): if the detected partner has no live channel this
      // symbol (stale pusch_pdu => |chPart|~0), the 2-layer MMSE degenerates and zeroes even the
      // self stream (outAbsMean=0, llr=0 observed). Fall through to normal MRC rather than destroy
      // the self decode. IRC only runs on genuinely-live co-scheduled pairs.
      long partE = 0;
      for (int a = 0; a < nb_rx_ant; a++)
        for (int i = 0; i < 32 && i < buffer_length; i++) partE += abs(chF2[1][a][i].r) + abs(chF2[1][a][i].i);
      if (partE < 8) mu_c_parte++; else mu_c_irc++;
      if (partE >= 8) {
      int32_t comp2buf[2 * nb_rx_ant][buffer_length] __attribute__((aligned(32)));
      int *comp2[2 * nb_rx_ant];
      for (int i = 0; i < 2 * nb_rx_ant; i++) { comp2[i] = comp2buf[i]; memset(comp2buf[i], 0, sizeof(int32_t) * buffer_length); }
      c16_t rho2[2][2][buffer_length] __attribute__((aligned(32)));
      c16_t mga[2][buffer_length] __attribute__((aligned(32)));
      c16_t mgb[2][buffer_length] __attribute__((aligned(32)));
      c16_t mgc[2][buffer_length] __attribute__((aligned(32)));
      memset(rho2, 0, sizeof(rho2)); memset(mga, 0, sizeof(mga)); memset(mgb, 0, sizeof(mgb)); memset(mgc, 0, sizeof(mgc));
      // Stage-2 joint detector. OAI's 2-layer path loops on rel15_ul->nrOfLayers (=1 for each
      // co-scheduled single-layer UE) -> only layer 0 compensated -> partner stream missing ->
      // degenerate MMSE -> zero output. Temporarily present a 2-layer context so BOTH chF2[0](self)
      // and chF2[1](partner) get matched-filtered. channel_compensation uses `symbol` ONLY for the
      // rxComp output offset [layer*nb_rx][symbol*buffer_length] (verified) -> pass 0 so it writes
      // into our compact offset-0 comp2buf; rho/mag are offset-0 regardless.
      // LOCAL COPY of the pdu with nrOfLayers=2 — NEVER mutate the shared pdu: the per-symbol Tpool
      // workers run in parallel, and another symbol's worker reading a transient nrOfLayers==2 in
      // the caller's layer-demap (line ~1491) indexes llrss[1]==NULL for this 1-layer UE ->
      // segfault at 0 across Tpool threads (the second DU-killer after the partner-scan NULL).
      nfapi_nr_pusch_pdu_t mu_pdu2 = *rel15_ul;
      mu_pdu2.nrOfLayers = 2;
      nr_ulsch_channel_compensation(buffer_length, nb_rx_ant, rxFext, chF2, mga, mgb, mgc, comp2, 2, rho2, &mu_pdu2, 0, output_shift);
      const int mu_nre = pusch_vars->ul_valid_re_per_slot[symbol];
      // layer 0 = self at comp2buf[0], layer 1 = partner at comp2buf[nb_rx_ant] (rxComp[layer*nb_rx]).
      if (rel15_ul->qam_mod_order <= 6) {
        // QPSK/16/64QAM: interference-aware ML joint demapper (uses rho). llr[0]=self; scratch=partner.
        int16_t mu_llr1[buffer_length * 8] __attribute__((aligned(32)));
        nr_ulsch_compute_ML_llr(pusch_vars, symbol,
                                (c16_t *)comp2buf[0], (c16_t *)comp2buf[nb_rx_ant],
                                mga[0], mga[1], llr[0], mu_llr1,
                                rho2[0][1], rho2[1][0], mu_nre, rel15_ul->qam_mod_order);
      } else {
        // 256QAM: MMSE-IRC to null the partner, then per-stream LLR of the separated self.
        nr_ulsch_mmse_2layers(comp2, buffer_length, nb_rx_ant, mga, mgb, mgc, chF2, rel15_ul->rb_size,
                              rel15_ul->qam_mod_order, pusch_vars->log2_maxh, symbol, mu_nre, nvar);
        nr_ulsch_compute_llr((int32_t *)comp2buf[0], mga[0], mgb[0], mgc[0], llr[0], mu_nre, symbol, rel15_ul->qam_mod_order);
      }
      // Stage-0 separation-health metric: chest magnitudes (self vs partner), post-eq output energy,
      // and LLR distribution of the separated self-stream. Diagnoses the DSP handoffs without a
      // reference: LLR~0 => extraction/scaling dead; LLR saturated => overflow; healthy+CRC-fail =>
      // residual interference (separation incomplete). Rate-limited, env OAI_UL_MU_IRC only.
      {
        static int m = 0;
        if ((m++ % 4000) == 0) {  // sample periodically through the whole run (catch post-steering state)
          int nre = pusch_vars->ul_valid_re_per_slot[symbol];
          long ch0 = 0, ch1 = 0, out0 = 0;
          for (int a = 0; a < nb_rx_ant; a++) {
            ch0 += abs(chF2[0][a][nre / 2].r) + abs(chF2[0][a][nre / 2].i);
            ch1 += abs(chF2[1][a][nre / 2].r) + abs(chF2[1][a][nre / 2].i);
          }
          const c16_t *o = (const c16_t *)comp2[0];
          for (int i = 0; i < nre; i++) out0 += abs(o[i].r) + abs(o[i].i);
          long labs = 0; int lmax = 0;
          for (int i = 0; i < nre * rel15_ul->qam_mod_order; i++) { int v = abs(llr[0][i]); labs += v; if (v > lmax) lmax = v; }
          // Spatial conditioning = per-RE correlation between the two UEs' channel vectors across
          // antennas, AVERAGED over the band (single-RE is noisy: the combined channel h0+h1*phasor
          // looks collinear per-RE even when the interpolated channel is not). corr2pct = mean over
          // REs of 100*|<h0,h1>|^2/(|h0|^2|h1|^2). ~0 = orthogonal (separable); ~100 = collinear
          // (rank-1, NO diversity -> IRC cannot separate). Answers "is it a spatial diversity issue".
          double corr_sum = 0.0; int corr_n = 0;
          for (int re = 0; re < nre; re += 12) {
            long ipr = 0, ipi = 0, e0 = 0, e1 = 0;
            for (int a = 0; a < nb_rx_ant; a++) {
              long h0r = chF2[0][a][re].r, h0i = chF2[0][a][re].i, h1r = chF2[1][a][re].r, h1i = chF2[1][a][re].i;
              ipr += h0r*h1r + h0i*h1i; ipi += h0r*h1i - h0i*h1r; e0 += h0r*h0r + h0i*h0i; e1 += h1r*h1r + h1i*h1i;
            }
            if (e0 > 0 && e1 > 0) { corr_sum += ((double)ipr*ipr + (double)ipi*ipi) / ((double)e0 * e1); corr_n++; }
          }
          int corr2pct = corr_n ? (int)(100.0 * corr_sum / corr_n) : -1;
          // POST-COMBINING SINR per UE (EVM-based, QPSK): ideal points are (+-m, +-m) with
          // m = mean(|I|,|Q|); SINR = signal power / error power around the nearest ideal point.
          // High (>20 dB) => MMSE-IRC output is clean and TB failures come from COVERAGE (fall-
          // through symbols decoded with interference); low => separation itself is weak.
          double sinr_db = -99.0;
          {
            long msum = 0;
            for (int i = 0; i < nre; i++) msum += abs(o[i].r) + abs(o[i].i);
            long m = nre ? msum / (2 * nre) : 0;
            double err = 0, sig = 0;
            for (int i = 0; i < nre; i++) {
              double er = (double)(abs(o[i].r) - m), ei = (double)(abs(o[i].i) - m);
              err += er * er + ei * ei; sig += 2.0 * (double)m * m;
            }
            if (err > 0 && sig > 0) sinr_db = 10.0 * log10(sig / err);
          }
          LOG_E(PHY, "[MU METRIC] rnti=%04x port=0x%x rnd=%d partner=%d sym=%d nre=%d |chSelf|=%ld |chPart|=%ld corr2pct=%d log2h=%d outAbsMean=%ld llrAbsMean=%ld llrMax=%d postSINR=%.1fdB cov(irc=%ld nopart=%ld rxe=%ld parte=%ld)\n",
                rel15_ul->rnti, rel15_ul->dmrs_ports, gNB->ulsch[ulsch_id].harq_process->round, partner, symbol, nre, ch0, ch1, corr2pct, pusch_vars->log2_maxh,
                nre ? out0 / nre : 0, (nre * rel15_ul->qam_mod_order) ? labs / (nre * rel15_ul->qam_mod_order) : 0, lmax,
                sinr_db, mu_c_irc, mu_c_nopart, mu_c_rxe, mu_c_parte);
        }
      }
      return;
      } // end if (partE >= 8): live partner -> IRC path; else fall through to normal MRC
    }
  }

  nr_ulsch_channel_compensation(buffer_length,
                                nb_rx_ant,
                                rxFext,
                                chFext,
                                rxF_ch_maga,
                                rxF_ch_magb,
                                rxF_ch_magc,
                                pusch_vars->rxdataF_comp,
                                nb_layer,
                                rho,
                                rel15_ul,
                                symbol,
                                output_shift);

  if (nb_layer == 1 && rel15_ul->transform_precoding == transformPrecoder_enabled && rel15_ul->qam_mod_order <= 6) {
    if (rel15_ul->qam_mod_order > 2)
      nr_freq_equalization(frame_parms,
                           (c16_t *)&pusch_vars->rxdataF_comp[0][symbol * buffer_length],
                           rxF_ch_maga[0],
                           rxF_ch_magb[0],
                           symbol,
                           pusch_vars->ul_valid_re_per_slot[symbol],
                           rel15_ul->qam_mod_order);
    nr_idft(&pusch_vars->rxdataF_comp[0][symbol * buffer_length], pusch_vars->ul_valid_re_per_slot[symbol]);
  }
  if (rel15_ul->pdu_bit_map & PUSCH_PDU_BITMAP_PUSCH_PTRS) {
    nr_pusch_ptrs_processing(gNB,
                             frame_parms,
                             rel15_ul,
                             ulsch_id,
                             slot,
                             symbol,
                             buffer_length);
    pusch_vars->ul_valid_re_per_slot[symbol] -= pusch_vars->ptrs_re_per_slot;
  }

  if (nb_layer == 2) {
    if (rel15_ul->qam_mod_order <= 6) {
      nr_ulsch_compute_ML_llr(pusch_vars,
                              symbol,
                              (c16_t *)&pusch_vars->rxdataF_comp[0][symbol * buffer_length],
                              (c16_t *)&pusch_vars->rxdataF_comp[nb_rx_ant][symbol * buffer_length],
                              rxF_ch_maga[0],
                              rxF_ch_maga[1],
                              llr[0],
                              llr[1],
                              rho[0][1],
                              rho[1][0],
                              pusch_vars->ul_valid_re_per_slot[symbol],
                              rel15_ul->qam_mod_order);
    }
    else {
      nr_ulsch_mmse_2layers((int32_t **)pusch_vars->rxdataF_comp,
                            buffer_length,
                            nb_rx_ant,
                            rxF_ch_maga,
                            rxF_ch_magb,
                            rxF_ch_magc,
                            chFext,
                            rel15_ul->rb_size,
                            rel15_ul->qam_mod_order,
                            pusch_vars->log2_maxh,
                            symbol,
                            pusch_vars->ul_valid_re_per_slot[symbol],
                            nvar);
    }
  }
  if (nb_layer != 2 || rel15_ul->qam_mod_order > 6)
    for (int aatx = 0; aatx < nb_layer; aatx++)
      nr_ulsch_compute_llr((int32_t *)&pusch_vars->rxdataF_comp[aatx * nb_rx_ant][symbol * buffer_length],
                           rxF_ch_maga[aatx],
                           rxF_ch_magb[aatx],
                           rxF_ch_magc[aatx],
                           llr[aatx],
                           pusch_vars->ul_valid_re_per_slot[symbol],
                           symbol,
                           rel15_ul->qam_mod_order);

  if (rel15_ul->pusch_data.tb_size >= 7
      && rel15_ul->pusch_data.rv_index == 0
      && (((rel15_ul->ul_dmrs_symb_pos >> symbol) & 0x01) == 0)) {
    NR_UL_gNB_HARQ_t *harq = gNB->ulsch[ulsch_id].harq_process;
    int valid_re = length < buffer_length ? length : buffer_length;
    record_gnb_pusch_rt_trace(frame,
                              slot,
                              rel15_ul->rnti,
                              gNB->ulsch[ulsch_id].harq_pid,
                              harq->round,
                              rel15_ul,
                              symbol,
                              rxFext[0],
                              chFext[0][0],
                              (c16_t *)&pusch_vars->rxdataF_comp[0][symbol * buffer_length],
                              valid_re);
  }
}

typedef struct puschSymbolProc_s {
  PHY_VARS_gNB *gNB;
  NR_DL_FRAME_PARMS *frame_parms;
  nfapi_nr_pusch_pdu_t *rel15_ul;
  int ulsch_id;
  uint32_t frame;
  int slot;
  int startSymbol;
  int numSymbols;
  int16_t *llr;
  int16_t *scramblingSequence;
  uint32_t nvar;
  int beam_nb;
  task_ans_t *ans;
  int bounces;  // Step-2(B) estimation-race requeue counter
  c16_t *pusch_ch_est_dmrs_interpl_slot_mem;
  c16_t *rxFext_slot_mem;
} puschSymbolProc_t;

static void nr_pusch_symbol_processing(void *arg)
{
  puschSymbolProc_t *rdata=(puschSymbolProc_t*)arg;

  PHY_VARS_gNB *gNB = rdata->gNB;
  NR_DL_FRAME_PARMS *frame_parms = rdata->frame_parms;
  nfapi_nr_pusch_pdu_t *rel15_ul = rdata->rel15_ul;
  int ulsch_id = rdata->ulsch_id;
  int slot = rdata->slot;
  NR_gNB_PUSCH *pusch_vars = &gNB->pusch_vars[ulsch_id];
  // Step-2(B): estimation-race requeue. If this slot's channel estimate is not ready (log2_maxh==0,
  // set by chest on completion), decoding is doomed (zero channel -> zero LLRs -> certain CRC fail
  // poisoning BLER/MCS). Re-push this task to the pool tail (bounded) so it re-runs microseconds
  // later when chest is done; ans completes on the successful re-run. MU-gated; default unchanged.
  { static int mu_rq = -1; if (mu_rq < 0) { const char *e = getenv("OAI_UL_MU_IRC"); mu_rq = (e && e[0]) ? 1 : 0; }
    extern volatile int g_mu_mimo_active;
    if (mu_rq && g_mu_mimo_active && rdata->bounces < 2) { // backstop only: two-phase ordering makes estimates ready before decode
      // partner estimate too: a co-scheduled decode with an unready PARTNER estimate skips IRC and
      // falls to MRC with the interference still on it (parte leak) — same race, same cure: defer.
      int unready = (pusch_vars->log2_maxh == 0);
      if (!unready) {
        for (int id = 0; id < gNB->max_nb_pusch; id++) {
          if (id == ulsch_id) continue;
          const NR_gNB_ULSCH_t *u = &gNB->ulsch[id];
          if (!u->active || u->harq_process == NULL) continue;
          if (u->frame != rdata->frame || u->slot != rdata->slot) continue;
          const nfapi_nr_pusch_pdu_t *p = &u->harq_process->ulsch_pdu;
          if (p->rb_size == rel15_ul->rb_size && p->rb_start == rel15_ul->rb_start && p->rnti != rel15_ul->rnti
              && gNB->pusch_vars[id].log2_maxh == 0) { unready = 1; break; }
        }
      }
      if (unready) {
        rdata->bounces++;
        task_t t = {.func = &nr_pusch_symbol_processing, .args = rdata};
        pushTpool(&gNB->threadPool, t);
        return;
      }
    } }
  for (int symbol = rdata->startSymbol; symbol < rdata->startSymbol + rdata->numSymbols; symbol++) {
    if (gNB->pusch_vars[ulsch_id].ul_valid_re_per_slot[symbol] == 0) 
      continue;
    int soffset = (slot % RU_RX_SLOT_DEPTH) * frame_parms->symbols_per_slot * frame_parms->ofdm_symbol_size;
    int buffer_length = ceil_mod(pusch_vars->ul_valid_re_per_slot[symbol] * NR_NB_SC_PER_RB, 16);
    int16_t llrs[rel15_ul->nrOfLayers][ceil_mod(buffer_length * rel15_ul->qam_mod_order, 64)];
    int16_t *llrss[rel15_ul->nrOfLayers];
    for (int l = 0; l < rel15_ul->nrOfLayers; l++)
      llrss[l] = llrs[l];

    inner_rx(gNB,
             ulsch_id,
             rdata->frame,
             slot,
             frame_parms,
             pusch_vars,
             rel15_ul,
             gNB->common_vars.rxdataF[rdata->beam_nb],
             (c16_t **)gNB->pusch_vars[ulsch_id].ul_ch_estimates,
             llrss,
             soffset,
             gNB->pusch_vars[ulsch_id].ul_valid_re_per_slot[symbol],
             symbol,
             gNB->pusch_vars[ulsch_id].log2_maxh,
             rdata->nvar,
             rdata->rxFext_slot_mem,
             rdata->pusch_ch_est_dmrs_interpl_slot_mem);

    int nb_re_pusch = gNB->pusch_vars[ulsch_id].ul_valid_re_per_slot[symbol];
    // layer de-mapping
    int16_t *llr_ptr = llrs[0];
    if (rel15_ul->nrOfLayers != 1) {
      llr_ptr = &rdata->llr[pusch_vars->llr_offset[symbol] * rel15_ul->nrOfLayers];
      for (int i = 0; i < (nb_re_pusch); i++)
        for (int l = 0; l < rel15_ul->nrOfLayers; l++)
          for (int m = 0; m < rel15_ul->qam_mod_order; m++)
            llr_ptr[i * rel15_ul->nrOfLayers * rel15_ul->qam_mod_order + l * rel15_ul->qam_mod_order + m] =
                llrss[l][i * rel15_ul->qam_mod_order + m];
    }
    // unscrambling
    int16_t *llr16 = (int16_t*)&rdata->llr[pusch_vars->llr_offset[symbol] * rel15_ul->nrOfLayers];
    int16_t *s = rdata->scramblingSequence + pusch_vars->llr_offset[symbol] * rel15_ul->nrOfLayers;
    const int end = nb_re_pusch * rel15_ul->qam_mod_order * rel15_ul->nrOfLayers;
    for (int i = 0; i < end; i++)
      llr16[i] = llr_ptr[i] * s[i];
  }

  // Task running in // completed
  completed_task_ans(rdata->ans);
}

static uint32_t average_u32(const uint32_t *x, uint16_t size)
{
  AssertFatal(size > 0 && x != NULL, "x is NULL or size is 0\n");

  uint64_t sum_x = 0;
  simde__m256i vec_sum = simde_mm256_setzero_si256();

  int i = 0;
  for (; i + 8 <= size; i += 8) {
    simde__m256i vec_data = simde_mm256_loadu_si256((simde__m256i *)&x[i]);
    vec_sum = simde_mm256_add_epi32(vec_sum, vec_data);
  }
  uint32_t *vec_sum32 = (uint32_t *)&vec_sum;
  for (int k = 0; k < 8; k++) {
    sum_x += vec_sum32[k];
  }
  for (; i < size; i++) {
    sum_x += x[i];
  }

  return (uint32_t)(sum_x / size);
}

int nr_rx_pusch_tp(PHY_VARS_gNB *gNB,
                   uint8_t ulsch_id,
                   uint32_t frame,
                   uint8_t slot,
                   unsigned char harq_pid,
                   int beam_nb)
{
  NR_DL_FRAME_PARMS *frame_parms = &gNB->frame_parms;
  nfapi_nr_pusch_pdu_t *rel15_ul = &gNB->ulsch[ulsch_id].harq_process->ulsch_pdu;

  NR_gNB_PUSCH *pusch_vars = &gNB->pusch_vars[ulsch_id];
  uint32_t bwp_start_subcarrier = ((rel15_ul->rb_start + rel15_ul->bwp_start) * NR_NB_SC_PER_RB + frame_parms->first_carrier_offset) % frame_parms->ofdm_symbol_size;
  LOG_D(PHY,"pusch %d.%d : bwp_start_subcarrier %d, rb_start %d, first_carrier_offset %d\n", frame,slot,bwp_start_subcarrier, rel15_ul->rb_start, frame_parms->first_carrier_offset);
  LOG_D(PHY,"pusch %d.%d : ul_dmrs_symb_pos %x\n",frame,slot,rel15_ul->ul_dmrs_symb_pos);

  // Memories to store data for data recording
  int buffer_length_slot = rel15_ul->rb_size * NR_NB_SC_PER_RB * 14; // 14 OFDM Symbols per slot
  int nb_rx_ant = frame_parms->nb_antennas_rx;
  int nb_layer = rel15_ul->nrOfLayers;

  // Initialize memory for DMRS signals
  c16_t pusch_dmrs_slot_mem[nb_layer * buffer_length_slot] __attribute__((aligned(32)));
  // Initialize memory for channel estimates based on DMRS positions
  c16_t pusch_ch_est_dmrs_pos_slot_mem[buffer_length_slot * nb_layer * nb_rx_ant] __attribute__((aligned(32)));
  // memory to store slot grid with channel coefficients based on DMRS positions after interpolation
  c16_t pusch_ch_est_dmrs_interpl_slot_mem[buffer_length_slot * nb_layer * nb_rx_ant] __attribute__((aligned(32)));
  // memory to store extracted data including PUSCH + DMRS
  c16_t rxFext_slot_mem[nb_rx_ant * buffer_length_slot] __attribute__((aligned(32)));

#if T_TRACER
  // Initialize memory for DMRS signals
  if (T_ACTIVE(T_GNB_PHY_UL_FD_DMRS))
    memset(pusch_dmrs_slot_mem, 0, sizeof(c16_t) * nb_layer * buffer_length_slot);

  // Initialize memory for channel estimates based on DMRS positions
  if (T_ACTIVE(T_GNB_PHY_UL_FD_CHAN_EST_DMRS_POS))
    memset(pusch_ch_est_dmrs_pos_slot_mem, 0, sizeof(c16_t) * buffer_length_slot * nb_layer * nb_rx_ant);

  // memory to store slot grid with channel coefficients based on DMRS positions after interpolation
  if (T_ACTIVE(T_GNB_PHY_UL_FD_CHAN_EST_DMRS_INTERPL))
    memset(pusch_ch_est_dmrs_interpl_slot_mem, 0, sizeof(c16_t) * buffer_length_slot * nb_layer * nb_rx_ant);

  // memory to store extracted data including PUSCH + DMRS
  if (T_ACTIVE(T_GNB_PHY_UL_FD_PUSCH_IQ))
    memset(rxFext_slot_mem, 0, sizeof(c16_t) * buffer_length_slot * nb_rx_ant);
#endif

  //----------------------------------------------------------
  //------------------- Channel estimation -------------------
  //----------------------------------------------------------
  start_meas(&gNB->ulsch_channel_estimation_stats);
  int max_ch = 0;
  uint32_t nvar = 0;
  int end_symbol = rel15_ul->start_symbol_index + rel15_ul->nr_of_symbols;
  // MU two-phase: phase 1 hoisted the chest (all co-scheduled UEs estimated before any decode,
  // so the joint receiver's partner estimates are always fresh). Reuse stored results here.
  const bool mu_skip_chest = pusch_vars->mu_chest_done != 0;
  if (mu_skip_chest) {
    max_ch = pusch_vars->mu_max_ch;
    nvar = pusch_vars->mu_nvar;
    pusch_vars->mu_chest_done = 0;
  }
  if (!mu_skip_chest)
  for (uint8_t symbol = rel15_ul->start_symbol_index; symbol < end_symbol; symbol++) {
    uint8_t dmrs_symbol_flag = (rel15_ul->ul_dmrs_symb_pos >> symbol) & 0x01;
    LOG_D(PHY, "symbol %d, dmrs_symbol_flag :%d\n", symbol, dmrs_symbol_flag);

    if (dmrs_symbol_flag == 1) {
      for (int nl = 0; nl < rel15_ul->nrOfLayers; nl++) {
        uint32_t nvar_tmp = 0;
        nr_pusch_channel_estimation(gNB,
                                    slot,
                                    nl,
                                    get_dmrs_port(nl, rel15_ul->dmrs_ports),
                                    symbol,
                                    ulsch_id,
                                    beam_nb,
                                    bwp_start_subcarrier,
                                    rel15_ul,
                                    &max_ch,
                                    &nvar_tmp,
                                    pusch_dmrs_slot_mem,
                                    pusch_ch_est_dmrs_pos_slot_mem);
        nvar += nvar_tmp;
      }
    }
  }

  if (!mu_skip_chest) {
  nvar /= (rel15_ul->nr_of_symbols * rel15_ul->nrOfLayers * frame_parms->nb_antennas_rx);

  allocCast2D(n0_subband_power,
              unsigned int,
              gNB->measurements.n0_subband_power,
              frame_parms->nb_antennas_rx,
              frame_parms->N_RB_UL,
              false);

  int start_sc = (rel15_ul->bwp_start + rel15_ul->rb_start) * NR_NB_SC_PER_RB;
  int middle_sc = frame_parms->ofdm_symbol_size - frame_parms->first_carrier_offset;
  int end_sc = (start_sc + rel15_ul->rb_size * NR_NB_SC_PER_RB - 1) % frame_parms->ofdm_symbol_size;
  for (int aarx = 0; aarx < frame_parms->nb_antennas_rx; aarx++) {
    pusch_vars->ulsch_power[aarx] = 0;
    pusch_vars->ulsch_noise_power[aarx] = 0;
    int64_t symb_energy = 0;

    for (uint8_t symbol = rel15_ul->start_symbol_index; symbol < end_symbol; symbol++) {
      int offset0 = ((slot % RU_RX_SLOT_DEPTH) * frame_parms->symbols_per_slot + symbol) * frame_parms->ofdm_symbol_size;
      int offset = offset0 + (frame_parms->first_carrier_offset + start_sc) % frame_parms->ofdm_symbol_size;
      c16_t *ul_ch = &gNB->common_vars.rxdataF[beam_nb][aarx][offset];
      if (end_sc < start_sc) {
        int64_t symb_energy_aux = signal_energy_nodc(ul_ch, middle_sc - start_sc) * (middle_sc - start_sc);
        ul_ch = &gNB->common_vars.rxdataF[beam_nb][aarx][offset0];
        symb_energy_aux += (signal_energy_nodc(ul_ch, end_sc + 1) * (end_sc + 1));
        symb_energy += symb_energy_aux / (rel15_ul->rb_size * NR_NB_SC_PER_RB);
      } else {
        symb_energy += signal_energy_nodc(ul_ch, rel15_ul->rb_size * NR_NB_SC_PER_RB);
      }
    }
    pusch_vars->ulsch_power[aarx] += (symb_energy / rel15_ul->nr_of_symbols);

    pusch_vars->ulsch_noise_power[aarx] +=
        average_u32(&n0_subband_power[aarx][rel15_ul->bwp_start + rel15_ul->rb_start], rel15_ul->rb_size);

    LOG_D(PHY,
          "aa %d, bwp_start%d, rb_start %d, rb_size %d: ulsch_power %d, ulsch_noise_power %d\n",
          aarx,
          rel15_ul->bwp_start,
          rel15_ul->rb_start,
          rel15_ul->rb_size,
          pusch_vars->ulsch_power[aarx],
          pusch_vars->ulsch_noise_power[aarx]);
  }

  // averaging time domain channel estimates
  if (gNB->chest_time == 1)
    nr_chest_time_domain_avg(frame_parms,
                             pusch_vars->ul_ch_estimates,
                             rel15_ul->nr_of_symbols,
                             rel15_ul->start_symbol_index,
                             rel15_ul->ul_dmrs_symb_pos,
                             rel15_ul->rb_size);
  } // !mu_skip_chest

  stop_meas(&gNB->ulsch_channel_estimation_stats);

  // MU two-phase, phase 1: store chest outputs and return; decode phase reuses them.
  if (gNB->mu_chest_only) {
    pusch_vars->mu_max_ch = max_ch;
    pusch_vars->mu_nvar = nvar;
    pusch_vars->mu_chest_done = 1;
    return 0;
  }
mu_chest_skipped:;

  start_meas(&gNB->rx_pusch_init_stats);

  // Scrambling initialization
  int number_dmrs_symbols = 0;
  for (int l = rel15_ul->start_symbol_index; l < end_symbol; l++)
    number_dmrs_symbols += ((rel15_ul->ul_dmrs_symb_pos)>>l) & 0x01;
  int nb_re_dmrs;
  if (rel15_ul->dmrs_config_type == pusch_dmrs_type1)
    nb_re_dmrs = 6*rel15_ul->num_dmrs_cdm_grps_no_data;
  else
    nb_re_dmrs = 4*rel15_ul->num_dmrs_cdm_grps_no_data;

  uint32_t unav_res = 0;
  if (rel15_ul->pdu_bit_map & PUSCH_PDU_BITMAP_PUSCH_PTRS) {
    uint16_t ptrsSymbPos = 0;
    set_ptrs_symb_idx(&ptrsSymbPos,
                      rel15_ul->nr_of_symbols,
                      rel15_ul->start_symbol_index,
                      1 << rel15_ul->pusch_ptrs.ptrs_time_density,
                      rel15_ul->ul_dmrs_symb_pos);
    int ptrsSymbPerSlot = get_ptrs_symbols_in_slot(ptrsSymbPos, rel15_ul->start_symbol_index, rel15_ul->nr_of_symbols);
    int n_ptrs = (rel15_ul->rb_size + rel15_ul->pusch_ptrs.ptrs_freq_density - 1) / rel15_ul->pusch_ptrs.ptrs_freq_density;
    unav_res = n_ptrs * ptrsSymbPerSlot;
  }

  // get how many bit in a slot //
  int G = nr_get_G(rel15_ul->rb_size,
                   rel15_ul->nr_of_symbols,
                   nb_re_dmrs,
                   number_dmrs_symbols, // number of dmrs symbols irrespective of single or double symbol dmrs
                   unav_res,
                   rel15_ul->qam_mod_order,
                   rel15_ul->nrOfLayers);
  gNB->ulsch[ulsch_id].unav_res = unav_res;

  // initialize scrambling sequence //
  int16_t scramblingSequence[G + 96] __attribute__((aligned(32)));

  nr_codeword_unscrambling_init(scramblingSequence, G, 0, rel15_ul->data_scrambling_id, rel15_ul->rnti);

  // first the computation of channel levels

  int nb_re_pusch = 0, meas_symbol = -1;
  for(meas_symbol = rel15_ul->start_symbol_index; meas_symbol < end_symbol; meas_symbol++) 
    if ((nb_re_pusch = get_nb_re_pusch(frame_parms, rel15_ul, meas_symbol)) > 0)
      break;

  AssertFatal(nb_re_pusch > 0 && meas_symbol >= 0,
              "nb_re_pusch %d cannot be 0 or meas_symbol %d cannot be negative here\n",
              nb_re_pusch,
              meas_symbol);

  // extract the first dmrs for the channel level computation
  // extract the data in the OFDM frame, to the start of the array
  int soffset = (slot % RU_RX_SLOT_DEPTH) * frame_parms->symbols_per_slot * frame_parms->ofdm_symbol_size;

  nb_re_pusch = ceil_mod(nb_re_pusch, 16);
  int dmrs_symbol;
  if (gNB->chest_time == 0)
    dmrs_symbol = get_valid_dmrs_idx_for_channel_est(rel15_ul->ul_dmrs_symb_pos, meas_symbol);
  else // average of channel estimates stored in first symbol
    dmrs_symbol = get_next_dmrs_symbol_in_slot(rel15_ul->ul_dmrs_symb_pos, rel15_ul->start_symbol_index, end_symbol);
  int size_est = nb_re_pusch * frame_parms->symbols_per_slot;
  __attribute__((aligned(32))) int ul_ch_estimates_ext[rel15_ul->nrOfLayers * frame_parms->nb_antennas_rx][size_est];
  memset(ul_ch_estimates_ext, 0, sizeof(ul_ch_estimates_ext));
  int buffer_length = rel15_ul->rb_size * NR_NB_SC_PER_RB;
  c16_t temp_rxFext[frame_parms->nb_antennas_rx][buffer_length] __attribute__((aligned(32)));
  for (int aarx = 0; aarx < frame_parms->nb_antennas_rx; aarx++) 
    for (int nl = 0; nl < rel15_ul->nrOfLayers; nl++)
      nr_ulsch_extract_rbs(gNB->common_vars.rxdataF[beam_nb][aarx],
                           (c16_t *)pusch_vars->ul_ch_estimates[nl * frame_parms->nb_antennas_rx + aarx],
                           temp_rxFext[aarx],
                           (c16_t*)&ul_ch_estimates_ext[nl * frame_parms->nb_antennas_rx + aarx][meas_symbol * nb_re_pusch],
                           soffset + meas_symbol * frame_parms->ofdm_symbol_size,
                           dmrs_symbol * frame_parms->ofdm_symbol_size,
                           aarx,
                           (rel15_ul->ul_dmrs_symb_pos >> meas_symbol) & 0x01, 
                           rel15_ul,
                           frame_parms);

  uint8_t shift_ch_ext = rel15_ul->nrOfLayers > 1 ? log2_approx(max_ch >> 11) : 0;

  //----------------------------------------------------------
  //--------------------- Channel Scaling --------------------
  //----------------------------------------------------------
  nr_scale_channel(size_est,
                   ul_ch_estimates_ext,
                   meas_symbol,
                   nb_re_pusch,
                   rel15_ul->nrOfLayers,
                   frame_parms->nb_antennas_rx,
                   shift_ch_ext);

  int avg[frame_parms->nb_antennas_rx*rel15_ul->nrOfLayers];
  nr_channel_level(meas_symbol,
                   size_est,
                   (c16_t (*)[size_est])ul_ch_estimates_ext,
                   frame_parms->nb_antennas_rx,
                   rel15_ul->nrOfLayers,
                   avg,
                   nb_re_pusch);

  int avgs = 0;
  for (int nl = 0; nl < rel15_ul->nrOfLayers; nl++)
    for (int aarx = 0; aarx < frame_parms->nb_antennas_rx; aarx++)
      avgs = cmax(avgs, avg[nl * frame_parms->nb_antennas_rx + aarx]);

  if (rel15_ul->nrOfLayers == 2 && rel15_ul->qam_mod_order > 6)
    pusch_vars->log2_maxh = (log2_approx(avgs) >> 1) - 3; // for MMSE
  else if (rel15_ul->nrOfLayers == 2)
    pusch_vars->log2_maxh = (log2_approx(avgs) >> 1) - 2 + log2_approx(frame_parms->nb_antennas_rx >> 1);
  else 
    pusch_vars->log2_maxh = (log2_approx(avgs) >> 1) + 1 + log2_approx(frame_parms->nb_antennas_rx >> 1);

  if (pusch_vars->log2_maxh < 0)
    pusch_vars->log2_maxh = 0;

  // nr_qpsk_llr() applies an additional >>4 after channel compensation. The log2_maxh formula
  // normalises the MF output to ~±1 amplitude, so the combined >>log2_maxh >>4 over-attenuates
  // in low-amplitude paths (e.g. vrtsim), producing int8 LLRs of only ~±2-5 — too weak for
  // the LDPC decoder to converge in max_iter iterations.  Absorb the QPSK downstream shift
  // into log2_maxh so the channel comp output is 16× larger and nr_qpsk_llr yields ~±50 LLRs.
  if (rel15_ul->qam_mod_order == 2) {
    const int qpsk_llr_shift = 4;
    pusch_vars->log2_maxh = (pusch_vars->log2_maxh > qpsk_llr_shift)
                            ? pusch_vars->log2_maxh - qpsk_llr_shift : 0;
  }

  static int pusch_demod_trace_count = 0;
  if (pusch_demod_trace_count < 0) {
    NR_UL_gNB_HARQ_t *harq = gNB->ulsch[ulsch_id].harq_process;
    LOG_I(PHY,
          "PUSCH demod trace %d.%d rnti %04x harq %d round %d rv %d rb %d+%d sym %d+%d dmrs 0x%x meas_sym %d dmrs_sym %d G %d avgs %d avg0 %d log2_maxh %d max_ch %d nvar %u qam %d mcs %d\n",
          frame,
          slot,
          rel15_ul->rnti,
          gNB->ulsch[ulsch_id].harq_pid,
          harq->round,
          rel15_ul->pusch_data.rv_index,
          rel15_ul->rb_start,
          rel15_ul->rb_size,
          rel15_ul->start_symbol_index,
          rel15_ul->nr_of_symbols,
          rel15_ul->ul_dmrs_symb_pos,
          meas_symbol,
          dmrs_symbol,
          G,
          avgs,
          avg[0],
          pusch_vars->log2_maxh,
          max_ch,
          nvar,
          rel15_ul->qam_mod_order,
          rel15_ul->mcs_index);
    pusch_demod_trace_count++;
  }

  stop_meas(&gNB->rx_pusch_init_stats);

  start_meas(&gNB->rx_pusch_symbol_processing_stats);
  int numSymbols = gNB->num_pusch_symbols_per_thread;
  int total_res = 0;
  int const loop_iter = CEILIDIV(rel15_ul->nr_of_symbols, numSymbols);
  puschSymbolProc_t arr[loop_iter];
  task_ans_t ans;
  init_task_ans(&ans, loop_iter);

  int sz_arr = 0;
  for(uint8_t task_index = 0; task_index < loop_iter; task_index++) {
    int symbol = task_index * numSymbols + rel15_ul->start_symbol_index;
    int res_per_task = 0;
    for (int s = 0; s < numSymbols && s + symbol < end_symbol; s++) {
      pusch_vars->ul_valid_re_per_slot[symbol+s] = get_nb_re_pusch(frame_parms,rel15_ul,symbol+s);
      pusch_vars->llr_offset[symbol+s] = ((symbol+s) == rel15_ul->start_symbol_index) ? 
                                         0 : 
                                         pusch_vars->llr_offset[symbol+s-1] + pusch_vars->ul_valid_re_per_slot[symbol+s-1] * rel15_ul->qam_mod_order;
      res_per_task += pusch_vars->ul_valid_re_per_slot[symbol + s];
    }
    total_res += res_per_task;
    if (res_per_task > 0) {
      puschSymbolProc_t *rdata = &arr[sz_arr];
      rdata->ans = &ans;
      ++sz_arr;

      rdata->gNB = gNB;
      rdata->frame_parms = frame_parms;
      rdata->rel15_ul = rel15_ul;
      rdata->slot = slot;
      rdata->startSymbol = symbol;
      // Last task processes remainder symbols
      rdata->numSymbols = task_index == loop_iter - 1 ? rel15_ul->nr_of_symbols - (loop_iter - 1) * numSymbols : numSymbols;
      rdata->ulsch_id = ulsch_id;
      rdata->frame = frame;
      rdata->llr = pusch_vars->llr;
      rdata->scramblingSequence = scramblingSequence;
      rdata->nvar = nvar;
      rdata->beam_nb = beam_nb;
      rdata->bounces = 0;
      rdata->rxFext_slot_mem = rxFext_slot_mem;
      rdata->pusch_ch_est_dmrs_interpl_slot_mem = pusch_ch_est_dmrs_interpl_slot_mem;

      if (rel15_ul->pdu_bit_map & PUSCH_PDU_BITMAP_PUSCH_PTRS) {
        nr_pusch_symbol_processing(rdata);
      } else {
        task_t t = {.func = &nr_pusch_symbol_processing, .args = rdata};
        pushTpool(&gNB->threadPool, t);
      }

      LOG_D(PHY, "%d.%d Added symbol %d to process, in pipe\n", frame, slot, symbol);
    } else {
      completed_task_ans(&ans);
    }
  } // symbol loop

#if T_TRACER
  // Get Time Stamp for T-tracer messages
  char trace_time_stamp_str[30];
  get_time_stamp_usec(trace_time_stamp_str);
  // trace_time_stamp_str = 8 bytes timestamp = YYYYMMDD
  //                      + 9 bytes timestamp = HHMMSSMMM
  // Not Ready for MIMO
  int dmrs_port = get_dmrs_port(0, rel15_ul->dmrs_ports);
  if (T_ACTIVE(T_GNB_PHY_UL_FD_DMRS)) {
    // Log GNB_PHY_UL_FD_DMRS using T-Tracer if activated
    // FORMAT = int,frame : int,slot : int,datetime_yyyymmdd : int,datetime_hhmmssmmm :
    // int,frame_type : int,freq_range : int,subcarrier_spacing : int,cyclic_prefix : int,symbols_per_slot :
    // int,Nid_cell : int,rnti :
    // int,rb_size : int,rb_start : int,start_symbol_index : int,nr_of_symbols :
    // int,qam_mod_order : int,mcs_index : int,mcs_table : int,nrOfLayers :
    // int,transform_precoding : int,dmrs_config_type : int,ul_dmrs_symb_pos :  int,number_dmrs_symbols : int,dmrs_port :
    // int,dmrs_nscid : int,nb_antennas_rx : int,number_of_bits : buffer,data
    T(T_GNB_PHY_UL_FD_DMRS,
      T_INT((int)frame),
      T_INT((int)slot),
      T_INT((int)split_time_stamp_and_convert_to_int(trace_time_stamp_str, 0, 8)),
      T_INT((int)split_time_stamp_and_convert_to_int(trace_time_stamp_str, 8, 9)),
      T_INT((int)frame_parms->frame_type), // Frame type (0 FDD, 1 TDD)  frame_structure
      T_INT((int)frame_parms->freq_range), // Frequency range (0 FR1, 1 FR2)
      T_INT((int)rel15_ul->subcarrier_spacing), // Subcarrier spacing (0 15kHz, 1 30kHz, 2 60kHz)
      T_INT((int)rel15_ul->cyclic_prefix), // Normal or extended prefix (0 normal, 1 extended)
      T_INT((int)frame_parms->symbols_per_slot), // Number of symbols per slot
      T_INT((int)frame_parms->Nid_cell),
      T_INT((int)rel15_ul->rnti),
      T_INT((int)rel15_ul->rb_size),
      T_INT((int)rel15_ul->rb_start),
      T_INT((int)rel15_ul->start_symbol_index), // start_ofdm_symbol
      T_INT((int)rel15_ul->nr_of_symbols), // num_ofdm_symbols
      T_INT((int)rel15_ul->qam_mod_order), // modulation
      T_INT((int)rel15_ul->mcs_index), // mcs
      T_INT((int)rel15_ul->mcs_table), // mcs_table_index
      T_INT((int)rel15_ul->nrOfLayers), // num_layer
      T_INT((int)rel15_ul->transform_precoding), // transformPrecoder_enabled = 0, transformPrecoder_disabled = 1
      T_INT((int)rel15_ul->dmrs_config_type), // dmrs_resource_map_config: pusch_dmrs_type1 = 0, pusch_dmrs_type2 = 1
      T_INT((int)rel15_ul->ul_dmrs_symb_pos), // used to derive the DMRS symbol positions
      T_INT((int)number_dmrs_symbols),
      // dmrs_start_ofdm_symbol
      // dmrs_duration_num_ofdm_symbols
      // dmrs_num_add_positions
      T_INT((int)dmrs_port), // dmrs_antenna_port
      T_INT((int)rel15_ul->scid), // dmrs_nscid
      T_INT((int)frame_parms->nb_antennas_rx), // rx antenna
      T_INT(0), // number_of_bits
      T_BUFFER((c16_t *)(&(pusch_dmrs_slot_mem[0])), rel15_ul->rb_size * NR_NB_SC_PER_RB * rel15_ul->nr_of_symbols * 4));
  }

  if (T_ACTIVE(T_GNB_PHY_UL_FD_CHAN_EST_DMRS_POS)) {
    // Log GNB_PHY_UL_FD_CHAN_EST_DMRS_POS using T-Tracer if activated
    // FORMAT = int,frame : int,slot : int,datetime_yyyymmdd : int,datetime_hhmmssmmm :
    // int,frame_type : int,freq_range : int,subcarrier_spacing : int,cyclic_prefix : int,symbols_per_slot :
    // int,Nid_cell : int,rnti :
    // int,rb_size : int,rb_start : int,start_symbol_index : int,nr_of_symbols :
    // int,qam_mod_order : int,mcs_index : int,mcs_table : int,nrOfLayers :
    // int,transform_precoding : int,dmrs_config_type : int,ul_dmrs_symb_pos :  int,number_dmrs_symbols : int,dmrs_port :
    // int,dmrs_nscid : int,nb_antennas_rx : int,number_of_bits : buffer,data
    T(T_GNB_PHY_UL_FD_CHAN_EST_DMRS_POS,
      T_INT((int)frame),
      T_INT((int)slot),
      T_INT((int)split_time_stamp_and_convert_to_int(trace_time_stamp_str, 0, 8)),
      T_INT((int)split_time_stamp_and_convert_to_int(trace_time_stamp_str, 8, 9)),
      T_INT((int)frame_parms->frame_type), // Frame type (0 FDD, 1 TDD)  frame_structure
      T_INT((int)frame_parms->freq_range), // Frequency range (0 FR1, 1 FR2)
      T_INT((int)rel15_ul->subcarrier_spacing), // Subcarrier spacing (0 15kHz, 1 30kHz, 2 60kHz)
      T_INT((int)rel15_ul->cyclic_prefix), // Normal or extended prefix (0 normal, 1 extended)
      T_INT((int)frame_parms->symbols_per_slot), // Number of symbols per slot
      T_INT((int)frame_parms->Nid_cell),
      T_INT((int)rel15_ul->rnti),
      T_INT((int)rel15_ul->rb_size),
      T_INT((int)rel15_ul->rb_start),
      T_INT((int)rel15_ul->start_symbol_index), // start_ofdm_symbol
      T_INT((int)rel15_ul->nr_of_symbols), // num_ofdm_symbols
      T_INT((int)rel15_ul->qam_mod_order), // modulation
      T_INT((int)rel15_ul->mcs_index), // mcs
      T_INT((int)rel15_ul->mcs_table), // mcs_table_index
      T_INT((int)rel15_ul->nrOfLayers), // num_layer
      T_INT((int)rel15_ul->transform_precoding), // transformPrecoder_enabled = 0, transformPrecoder_disabled = 1
      T_INT((int)rel15_ul->dmrs_config_type), // dmrs_resource_map_config: pusch_dmrs_type1 = 0, pusch_dmrs_type2 = 1
      T_INT((int)rel15_ul->ul_dmrs_symb_pos), // used to derive the DMRS symbol positions
      T_INT((int)number_dmrs_symbols),
      // dmrs_start_ofdm_symbol
      // dmrs_duration_num_ofdm_symbols
      // dmrs_num_add_positions
      T_INT((int)dmrs_port), // dmrs_antenna_port
      T_INT((int)rel15_ul->scid), // dmrs_nscid
      T_INT((int)frame_parms->nb_antennas_rx), // rx antenna
      T_INT(0), // number_of_bits
      T_BUFFER((c16_t *)(&(pusch_ch_est_dmrs_pos_slot_mem[0])), rel15_ul->rb_size * NR_NB_SC_PER_RB * rel15_ul->nr_of_symbols * 4));
  }

  if (T_ACTIVE(T_GNB_PHY_UL_FD_PUSCH_IQ)) {
    // Log GNB_PHY_UL_FD_PUSCH_IQ using T-Tracer if activated
    // FORMAT = int,frame : int,slot : int,datetime_yyyymmdd : int,datetime_hhmmssmmm :
    // int,frame_type : int,freq_range : int,subcarrier_spacing : int,cyclic_prefix : int,symbols_per_slot :
    // int,Nid_cell : int,rnti :
    // int,rb_size : int,rb_start : int,start_symbol_index : int,nr_of_symbols :
    // int,qam_mod_order : int,mcs_index : int,mcs_table : int,nrOfLayers :
    // int,transform_precoding : int,dmrs_config_type : int,ul_dmrs_symb_pos :  int,number_dmrs_symbols : int,dmrs_port :
    // int,dmrs_nscid : int,nb_antennas_rx : int,number_of_bits : buffer,data

    T(T_GNB_PHY_UL_FD_PUSCH_IQ,
      T_INT((int)frame),
      T_INT((int)slot),
      T_INT((int)split_time_stamp_and_convert_to_int(trace_time_stamp_str, 0, 8)),
      T_INT((int)split_time_stamp_and_convert_to_int(trace_time_stamp_str, 8, 9)),
      T_INT((int)frame_parms->frame_type), // Frame type (0 FDD, 1 TDD)  frame_structure
      T_INT((int)frame_parms->freq_range), // Frequency range (0 FR1, 1 FR2)
      T_INT((int)rel15_ul->subcarrier_spacing), // Subcarrier spacing (0 15kHz, 1 30kHz, 2 60kHz)
      T_INT((int)rel15_ul->cyclic_prefix), // Normal or extended prefix (0 normal, 1 extended)
      T_INT((int)frame_parms->symbols_per_slot), // Number of symbols per slot
      T_INT((int)frame_parms->Nid_cell),
      T_INT((int)rel15_ul->rnti),
      T_INT((int)rel15_ul->rb_size),
      T_INT((int)rel15_ul->rb_start),
      T_INT((int)rel15_ul->start_symbol_index), // start_ofdm_symbol
      T_INT((int)rel15_ul->nr_of_symbols), // num_ofdm_symbols
      T_INT((int)rel15_ul->qam_mod_order), // modulation
      T_INT((int)rel15_ul->mcs_index), // mcs
      T_INT((int)rel15_ul->mcs_table), // mcs_table_index
      T_INT((int)rel15_ul->nrOfLayers), // num_layer
      T_INT((int)rel15_ul->transform_precoding), // transformPrecoder_enabled = 0, transformPrecoder_disabled = 1
      T_INT((int)rel15_ul->dmrs_config_type), // dmrs_resource_map_config: pusch_dmrs_type1 = 0, pusch_dmrs_type2 = 1
      T_INT((int)rel15_ul->ul_dmrs_symb_pos), // used to derive the DMRS symbol positions
      T_INT((int)number_dmrs_symbols),
      // dmrs_start_ofdm_symbol
      // dmrs_duration_num_ofdm_symbols
      // dmrs_num_add_positions
      T_INT((int)dmrs_port), // dmrs_antenna_port
      T_INT((int)rel15_ul->scid), // dmrs_nscid
      T_INT((int)frame_parms->nb_antennas_rx), // rx antenna
      T_INT(0), // number_of_bits
      T_BUFFER((c16_t *)(&(rxFext_slot_mem[0])),
               rel15_ul->rb_size * NR_NB_SC_PER_RB * rel15_ul->nr_of_symbols * frame_parms->nb_antennas_rx * 4));
  }
  if (T_ACTIVE(T_GNB_PHY_UL_FD_CHAN_EST_DMRS_INTERPL)) {
    // Log pusch_ch_est_dmrs_interpl_slot_mem using T-Tracer if activated
    // FORMAT = int,frame : int,slot : int,datetime_yyyymmdd : int,datetime_hhmmssmmm :
    // int,frame_type : int,freq_range : int,subcarrier_spacing : int,cyclic_prefix : int,symbols_per_slot :
    // int,Nid_cell : int,rnti :
    // int,rb_size : int,rb_start : int,start_symbol_index : int,nr_of_symbols :
    // int,qam_mod_order : int,mcs_index : int,mcs_table : int,nrOfLayers :
    // int,transform_precoding : int,dmrs_config_type : int,ul_dmrs_symb_pos :  int,number_dmrs_symbols : int,dmrs_port :
    // int,dmrs_nscid : int,nb_antennas_rx : int,number_of_bits : buffer,data

    T(T_GNB_PHY_UL_FD_CHAN_EST_DMRS_INTERPL,
      T_INT((int)frame),
      T_INT((int)slot),
      T_INT((int)split_time_stamp_and_convert_to_int(trace_time_stamp_str, 0, 8)),
      T_INT((int)split_time_stamp_and_convert_to_int(trace_time_stamp_str, 8, 9)),
      T_INT((int)frame_parms->frame_type), // Frame type (0 FDD, 1 TDD)  frame_structure
      T_INT((int)frame_parms->freq_range), // Frequency range (0 FR1, 1 FR2)
      T_INT((int)rel15_ul->subcarrier_spacing), // Subcarrier spacing (0 15kHz, 1 30kHz, 2 60kHz)
      T_INT((int)rel15_ul->cyclic_prefix), // Normal or extended prefix (0 normal, 1 extended)
      T_INT((int)frame_parms->symbols_per_slot), // Number of symbols per slot
      T_INT((int)frame_parms->Nid_cell),
      T_INT((int)rel15_ul->rnti),
      T_INT((int)rel15_ul->rb_size),
      T_INT((int)rel15_ul->rb_start),
      T_INT((int)rel15_ul->start_symbol_index), // start_ofdm_symbol
      T_INT((int)rel15_ul->nr_of_symbols), // num_ofdm_symbols
      T_INT((int)rel15_ul->qam_mod_order), // modulation
      T_INT((int)rel15_ul->mcs_index), // mcs
      T_INT((int)rel15_ul->mcs_table), // mcs_table_index
      T_INT((int)rel15_ul->nrOfLayers), // num_layer
      T_INT((int)rel15_ul->transform_precoding), // transformPrecoder_enabled = 0, transformPrecoder_disabled = 1
      T_INT((int)rel15_ul->dmrs_config_type), // dmrs_resource_map_config: pusch_dmrs_type1 = 0, pusch_dmrs_type2 = 1
      T_INT((int)rel15_ul->ul_dmrs_symb_pos), // used to derive the DMRS symbol positions
      T_INT((int)number_dmrs_symbols),
      // dmrs_start_ofdm_symbol
      // dmrs_duration_num_ofdm_symbols
      // dmrs_num_add_positions
      T_INT((int)dmrs_port), // dmrs_antenna_port
      T_INT((int)rel15_ul->scid), // dmrs_nscid
      T_INT((int)frame_parms->nb_antennas_rx), // rx antenna
      T_INT(0), // number_of_bits
      T_BUFFER(
          (c16_t *)pusch_ch_est_dmrs_interpl_slot_mem,
          rel15_ul->rb_size * NR_NB_SC_PER_RB * rel15_ul->nr_of_symbols * frame_parms->nb_antennas_rx * rel15_ul->nrOfLayers * 4));
  }
#endif

  join_task_ans(&ans);
  stop_meas(&gNB->rx_pusch_symbol_processing_stats);

  // Copy the data to the scope. This cannot be performed in one call to gNBscopeCopy because the data is not contiguous in the
  // buffer due to reference symbol extraction and padding. The gNBscopeCopy call is broken up into steps: trylock, copy, unlock.
  metadata mt = {.slot = slot, .frame = frame};
  if (gNBTryLockScopeData(gNB, gNBPuschRxIq, sizeof(c16_t), 1, total_res, &mt)) {
    int buffer_length = ceil_mod(rel15_ul->rb_size * NR_NB_SC_PER_RB, 16);
    size_t offset = 0;
    for (uint8_t symbol = rel15_ul->start_symbol_index; symbol < (rel15_ul->start_symbol_index + rel15_ul->nr_of_symbols);
         symbol++) {
      gNBscopeCopyUnsafe(gNB,
                         gNBPuschRxIq,
                         &pusch_vars->rxdataF_comp[0][symbol * buffer_length],
                         sizeof(c16_t) * pusch_vars->ul_valid_re_per_slot[symbol],
                         offset,
                         symbol - rel15_ul->start_symbol_index);
      offset += sizeof(c16_t) * pusch_vars->ul_valid_re_per_slot[symbol];
    }
    gNBunlockScopeData(gNB, gNBPuschRxIq)
  }
  uint32_t total_llrs = total_res * rel15_ul->qam_mod_order * rel15_ul->nrOfLayers;
  gNBscopeCopyWithMetadata(gNB, gNBPuschLlr, pusch_vars->llr, sizeof(c16_t), 1, total_llrs, 0, &mt);
  return 0;
}
