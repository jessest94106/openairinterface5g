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
#include "catb_weight_ring.h"


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
    // Wide-accumulator MRC: sum conj(H)*Y products and |h|^2 across antennas in
    // int32 lanes, apply output_shift once (rounded) after the full sum, then
    // saturating-pack to int16. The former per-antenna shift + wrapping int16
    // accumulation lost precision and could wrap at high antenna counts,
    // corrupting LLRs (see RX16_TBLER_ROOT_CAUSE_REPORT.md).
    const simde__m256i round_bias = simde_mm256_set1_epi32(output_shift > 0 ? 1 << (output_shift - 1) : 0);
    for (int i = 0; i < buffer_length >> 3; i++) {
      simde__m256i acc_re = simde_mm256_setzero_si256();
      simde__m256i acc_im = simde_mm256_setzero_si256();
      simde__m256i acc_mag = simde_mm256_setzero_si256();
      for (int aarx = 0; aarx < nb_rx_ant; aarx++) {
        const simde__m256i ch = ((simde__m256i *)chFext[aatx][aarx])[i];
        const simde__m256i rx = ((simde__m256i *)rxFext[aarx])[i];
        // same re/im decomposition as oai_mm256_cpx_mult_conj, without the shift/pack
        acc_re = simde_mm256_add_epi32(acc_re, simde_mm256_madd_epi16(ch, rx));
        acc_im = simde_mm256_add_epi32(acc_im, simde_mm256_madd_epi16(oai_mm256_swap(oai_mm256_conj(ch)), rx));
        if (mod_order > 2)
          acc_mag = simde_mm256_add_epi32(acc_mag, simde_mm256_madd_epi16(ch, ch)); // |h|^2
      }
      acc_re = simde_mm256_srai_epi32(simde_mm256_add_epi32(acc_re, round_bias), output_shift);
      acc_im = simde_mm256_srai_epi32(simde_mm256_add_epi32(acc_im, round_bias), output_shift);
      rxComp_256[i] = oai_mm256_pack(acc_re, acc_im);

      if (mod_order > 2) {
        simde__m256i mag = simde_mm256_srai_epi32(simde_mm256_add_epi32(acc_mag, round_bias), output_shift);
        // pack and duplicate
        mag = simde_mm256_packs_epi32(mag, mag);
        mag = simde_mm256_unpacklo_epi16(mag, mag);

        rxF_ch_maga_256[i] = simde_mm256_mulhrs_epi16(mag, QAM_ampa_256);

        if (mod_order > 4)
          rxF_ch_magb_256[i] = simde_mm256_mulhrs_epi16(mag, QAM_ampb_256);

        if (mod_order > 6)
          rxF_ch_magc_256[i] = simde_mm256_mulhrs_epi16(mag, QAM_ampc_256);
      }
    }
    if (nb_layers > 1) {
      for (int aarx = 0; aarx < nb_rx_ant; aarx++) {
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

// Cat-B STEP 2: compute the UL MMSE combining weights and publish them for the RU.
// PASSIVE — nothing here changes decoding; the DU still combines exactly as before.
//
// W must be COMPUTED, not forwarded: OAI never materialises a weight matrix. The receiver
// builds the 2x2 Gram H^H*H and applies its inverse to matched-filtered data, so the N_ant x 2
// combiner exists only implicitly. Here we form it explicitly, per PRB:
//     G  = H^H H + nvar*I      (2x2, H is [layer][ant] at the PRB centre RE)
//     W  = G^-1 H^H            (2 x n_ant)
// One PRB-centre RE rather than an average over 12: MMSE weights vary slowly across a PRB
// (that is the premise of per-PRB weighting), and this runs in the DU's per-slot hot path.
// NOTE: double precision, normalised per PRB — magnitude is arbitrary for a combiner,
// only the relative pattern across antennas carries information, and the STEP 2 acceptance
// check is that the per-antenna PHASE matches the configured CDL arrival angles.
// Returns the number of PRBs left ZEROED because G was singular. A publish that zeroed every PRB
// still "succeeds" — presence flags have lied here before (§17 bug 4), so the caller logs this.
static int catb_publish_weights(catb_weight_ring_t *ring,
                                 int frame,
                                 int slot,
                                 uint16_t rnti,
                                 int rb_start,
                                 int nb_rb,
                                 int nb_rx_ant,
                                 uint32_t buffer_length,
                                 const c16_t ch[][nb_rx_ant][buffer_length],
                                 uint32_t nvar,
                                 int re_per_prb,
                                 int src)
{
  if (ring == NULL || nb_rx_ant > CATB_MAX_ANT || nb_rb > CATB_MAX_PRB || nb_rb <= 0)
    return -1;
  int n_sing = 0;
  double dbg_l1 = 0.0, dbg_scale = 0.0, dbg_wr = 0.0, dbg_wi = 0.0;
  int dbg_q_r = 0, dbg_q_i = 0, dbg_break_prb = -1, dbg_written = 0, dbg_wzero = 0;
  double dbg_hspread = -1.0, dbg_wspread = -1.0, dbg_dd = 0.0;
  const int L = CATB_MAX_LAYERS;
  catb_weight_rec_t *rec = catb_ring_begin(ring);
  rec->frame = (uint16_t)frame;
  rec->slot = (uint16_t)slot;
  rec->rnti = rnti;
  rec->rb_start = (uint16_t)rb_start;
  rec->n_prb = (uint16_t)nb_rb;
  rec->n_ant = (uint8_t)nb_rx_ant;
  rec->n_layers = (uint8_t)L;

  for (int prb = 0; prb < nb_rb; prb++) {
    // PRB centre. re_per_prb is 12 on a DATA symbol but only 6 on a DMRS symbol, because
    // nr_ulsch_extract_rbs() packs just the DMRS-carrying REs there. Hardcoding 12 while
    // publishing from a reference symbol read PAST the filled region for mid/high PRBs and
    // produced an ALL-ZERO weight vector on the wire (observed: w[0..3]=(0,0)(0,0)).
    const uint32_t re = (uint32_t)prb * (uint32_t)re_per_prb + (uint32_t)(re_per_prb / 2);
    if (re >= buffer_length) {
      dbg_break_prb = prb; // which PRB the loop stopped at — measured last_nz says 53 of 106
      break;
    }
    dbg_written = prb + 1;
    double hr[CATB_MAX_LAYERS][CATB_MAX_ANT], hi[CATB_MAX_LAYERS][CATB_MAX_ANT];
    for (int l = 0; l < L; l++)
      for (int a = 0; a < nb_rx_ant; a++) {
        hr[l][a] = ch[l][a][re].r;
        hi[l][a] = ch[l][a][re].i;
      }
    // G = H^H H + nvar I  (Hermitian 2x2)
    double gr[2][2] = {{0}}, gi[2][2] = {{0}};
    for (int i = 0; i < L; i++)
      for (int j = 0; j < L; j++) {
        double sr = 0, si = 0;
        for (int a = 0; a < nb_rx_ant; a++) {
          sr += hr[i][a] * hr[j][a] + hi[i][a] * hi[j][a];   // conj(h_i)·h_j
          si += hr[i][a] * hi[j][a] - hi[i][a] * hr[j][a];
        }
        // REGULARISER MUST BE NON-ZERO. In the SU case (§20) the partner channel is zeroed, so G
        // is Hermitian-diagonal and det collapses to exactly (|h0|^2 + nvar) * nvar — PROPORTIONAL
        // to nvar. With nvar == 0 (it is a uint32_t, so exactly 0 is reachable) the dd < 1e-9 guard
        // below fires on every PRB and an ALL-ZERO vector goes on the wire: measured
        // w[0..3]=(0,0)(0,0), which the RU then combines with, producing "MSG3 ULSCH with no
        // signal" and blocking every subsequent attach. The nvar cancels analytically in
        // w[0] = conj(h0)/(|h0|^2 + nvar), so flooring it at 1 changes the answer by ~1e-5 while
        // keeping G invertible. The MU path is unaffected (its det does not vanish with nvar).
        const double reg = (nvar > 0) ? (double)nvar : 1.0;
        gr[i][j] = sr + (i == j ? reg : 0.0);
        gi[i][j] = si;
      }
    // inv(G) for 2x2: [[g11,-g01],[-g10,g00]] / det
    const double dr = gr[0][0] * gr[1][1] - gi[0][0] * gi[1][1] - (gr[0][1] * gr[1][0] - gi[0][1] * gi[1][0]);
    const double di = gr[0][0] * gi[1][1] + gi[0][0] * gr[1][1] - (gr[0][1] * gi[1][0] + gi[0][1] * gr[1][0]);
    const double dd = dr * dr + di * di;
    if (dd < 1e-9) {
      n_sing++;
      continue; // singular (collinear UEs) — leave this PRB zeroed rather than emit garbage
    }
    const double invr[2][2] = {{gr[1][1], -gr[0][1]}, {-gr[1][0], gr[0][0]}};
    const double invi[2][2] = {{gi[1][1], -gi[0][1]}, {-gi[1][0], gi[0][0]}};
    // W = G^-1 H^H  ->  w[l][a] = sum_i invG[l][i] * conj(h_i[a])
    double wr[CATB_MAX_LAYERS][CATB_MAX_ANT], wi[CATB_MAX_LAYERS][CATB_MAX_ANT];
    for (int l = 0; l < L; l++)
      for (int a = 0; a < nb_rx_ant; a++) {
        double sr = 0, si = 0;
        for (int i = 0; i < L; i++) {
          // (invG[l][i]/det) * conj(h_i[a])
          const double ar = (invr[l][i] * dr + invi[l][i] * di) / dd;
          const double ai = (invi[l][i] * dr - invr[l][i] * di) / dd;
          sr += ar * hr[i][a] + ai * hi[i][a];
          si += ai * hr[i][a] - ar * hi[i][a];
        }
        wr[l][a] = sr;
        wi[l][a] = si;
      }
    // ONE normalisation, not two (§23). The only constraint that matters is the RU's combine:
    // y = sum_a (x_a * conj(w_a)) >> 15 saturates unless sum_a(|wr|+|wi|) <= 32768. Normalising
    // here to max-component 29000 and THEN L1-rescaling by ~0.12 in catb_bfw_attach quantised to
    // Q15 twice and threw away ~3 bits — measured w[0]=(0,0) on BOTH sides, i.e. the weakest
    // antennas vanished entirely and their terms dropped out of both y and h_eff. Normalise once,
    // per (PRB, layer), straight to the L1 budget. Only the DIRECTION of w carries information
    // (a common scale cancels between y = w^H x and h_eff = w^H H), so this is free.
    for (int l = 0; l < L; l++) {
      double l1 = 0.0;
      for (int a = 0; a < nb_rx_ant; a++)
        l1 += fabs(wr[l][a]) + fabs(wi[l][a]);
      const double scale = (l1 > 0.0) ? (32767.0 / l1) : 0.0;
      for (int a = 0; a < nb_rx_ant; a++) {
        const size_t k = catb_w_index(prb, l, a, L, nb_rx_ant);
        rec->w[2 * k] = (int16_t)lrint(wr[l][a] * scale);
        rec->w[2 * k + 1] = (int16_t)lrint(wi[l][a] * scale);
      }
      // Capture what was ACTUALLY WRITTEN at the PRB catb_bfw_attach will read (mid-band, layer 0).
      // The wire showed w=(0,0) while singular_prb=0/106 said the maths was fine, so the loss is
      // between "computed" and "quantised". Log the raw double, l1, scale and the stored int16 —
      // that interval contains the bug and nothing else does.
      if (l == 0 && prb == 26) { // PRB 26 to match the RU's [CATB XPROF]; nb_rb/2 straddles DC
        dbg_l1 = l1;
        dbg_scale = scale;
        dbg_wr = wr[0][0];
        dbg_wi = wi[0][0];
        const size_t k0 = catb_w_index(prb, 0, 0, L, nb_rx_ant);
        dbg_q_r = rec->w[2 * k0];
        dbg_q_i = rec->w[2 * k0 + 1];
        // DEGENERACY DIAGNOSIS (§26). Observed w[0..3]=(22380,-768)(0,0)(0,0)(0,0): all energy on
        // antenna 0, the other 15 quantised to zero, so there is no array gain and "coherence"
        // is trivially 1 (a one-term sum). Three candidate causes, and these four numbers
        // separate them:
        //   h_max/h_min ~ 1  + w_max/w_min huge  => the SOLVE is degenerate (ill-conditioned G)
        //   h_max/h_min huge                     => the CHANNEL ESTIMATE is on one antenna only
        //   w spread fine before scaling         => the L1 NORMALISATION is crushing the rest
        // dd is the Gram determinant magnitude; a tiny dd means G is near-singular.
        double hmax = 0.0, hmin = 1e300, wmax = 0.0, wmin = 1e300;
        int n_wzero = 0;
        for (int a = 0; a < nb_rx_ant; a++) {
          const double hm = fabs(hr[0][a]) + fabs(hi[0][a]);
          const double wm = fabs(wr[0][a]) + fabs(wi[0][a]);
          if (hm > hmax) hmax = hm;
          if (hm < hmin) hmin = hm;
          if (wm > wmax) wmax = wm;
          if (wm < wmin) wmin = wm;
          if (lrint(wr[0][a] * scale) == 0 && lrint(wi[0][a] * scale) == 0) n_wzero++;
        }
        dbg_hspread = hmin > 0.0 ? hmax / hmin : -1.0;
        dbg_wspread = wmin > 0.0 ? wmax / wmin : -1.0;
        dbg_wzero = n_wzero;
        dbg_dd = dd;
        // ANTENNA FINGERPRINT, DU side (§26). Compare against the RU's [CATB XPROF] |x_a| profile
        // at the same PRB. Same ordering => the mapping is 1:1 and the fault is elsewhere; a
        // permutation => w_a is applied to the wrong x_a, giving exactly 1/sqrt(16).
        {
          char hb[320];
          int ho = 0;
          for (int a = 0; a < nb_rx_ant; a++)
            ho += snprintf(hb + ho, sizeof(hb) - ho, "%ld,",
                           (long)(fabs(hr[0][a]) + fabs(hi[0][a])));
          // rb_start/rb_size so the RU's ABSOLUTE PRB can be matched to this ALLOCATION-
          // relative one; nr_ulsch_extract_rbs packs from rb_start, so prb here is an offset.
          LOG_A(PHY, "[CATB HPROF] prb=%d rb_start=%d rb_size=%d |h_a|=%s\n",
                prb, rb_start, nb_rb, hb);
        }
      }
    }
  }
  {
    // TRUNCATION is the fault (measured: last_nz=1695 => only 53 of 106 PRBs ever written, and
    // n_prb/2 lands on the first hole). Log EVERY truncating publish, not 1-in-500 — a rare probe
    // already cost a run by sampling the wrong path. `src` names the call site: the two pass
    // different buffer_length/re_per_prb and only one of them truncates.
    // Sample PER CALL SITE. A single shared counter at %500 only ever caught src=1 (which is
    // healthy: nonzero=3392/3392), so src=2's geometry was never observed. The reader's
    // last_nz=1695 equals (105*2+1)*8+7 exactly — an 8-ANTENNA layout — so nb_rx_ant is the field
    // that matters and it was never printed.
    static long nq_src[3] = {0}, ntrunc = 0;
    long nq = nq_src[(src >= 0 && src <= 2) ? src : 0]++;
    if (dbg_written < nb_rb) {
      if ((ntrunc++ % 200) == 0)
        LOG_A(PHY, "[CATB TRUNC] src=%d nb_rb=%d written=%d break_prb=%d re_per_prb=%d"
                   " buffer_length=%u (need %d)\n",
              src, nb_rb, dbg_written, dbg_break_prb, re_per_prb, buffer_length,
              (nb_rb - 1) * re_per_prb + re_per_prb / 2 + 1);
    }
    if ((nq % 100) == 1) {
      // WRITER-SIDE SCAN, byte-identical to the reader's in catb_bfw_attach. The writer reports
      // written=106 while the reader sees data only in elements 0..1695 (53 PRBs). Running the
      // SAME scan on both sides of the ring is the only way to tell "the writer did not write it"
      // from "the record changed in transit" — every other framing of this has been ambiguous.
      long w_nz = 0, w_first = -1, w_last = -1;
      const long w_elem = (long)nb_rb * L * nb_rx_ant;
      for (long e = 0; e < w_elem && e < (long)(sizeof(rec->w) / (2 * sizeof(rec->w[0]))); e++)
        if (rec->w[2 * e] || rec->w[2 * e + 1]) {
          if (w_first < 0)
            w_first = e;
          w_last = e;
          w_nz++;
        }
      LOG_A(PHY, "[CATB QUANT] src=%d nb_rb=%d n_ant=%d written=%d q=(%d,%d) n_sing=%d"
                 " | WRITER nonzero=%ld/%ld last=%ld"
                 " | DEGEN h_spread=%.2e w_spread=%.2e w_zeroed=%d/%d dd=%.3e\n",
            src, nb_rb, nb_rx_ant, dbg_written, dbg_q_r, dbg_q_i, n_sing,
            w_nz, w_elem, w_last,
            dbg_hspread, dbg_wspread, dbg_wzero, nb_rx_ant, dbg_dd);
    }
  }
  catb_ring_commit(ring, rec);
  return n_sing;
}

// MMSE Rx function: nr_ulsch_mmse_2layers()
// Cat-B: delayed channel-estimate view, published per UE per antenna. Read-only for
// consumers; NULL means "use the live estimate".
// STEP 4 ring: keyed by REAL ulsch_id. gNB->max_nb_pusch = MAX_MOBILES_PER_GNB * buffer_ul_slots
// (nr_init.c:467) and measures 144 in this lab, so the old `% 8` aliased 18 distinct decodes onto
// each slot: writes and reads landed in different UEs' entries, `view` came out (nil) in every
// sample, and the delayed estimate was NEVER delivered. Every previous "failure" was this block's
// own malloc/memcpy cost, not staleness.
// Key by RNTI, NOT ulsch_id. ulsch_id is a rotating index into a pool of gNB->max_nb_pusch (=144
// here) decode slots, reassigned per grant, so the SAME UE gets a DIFFERENT ulsch_id every slot:
// keying by it writes each entry once and never builds that UE's history, and the delayed read
// returns another UE's leftovers (measured: view non-nil but the partner checksum ~0). RNTI is the
// stable per-UE identity across slots.
#define CATB_MAX_UE 8
static int32_t *catb_hview[CATB_MAX_UE][16];

// Dense slot for an RNTI, assigned on first sight. Linear scan over <=8 entries; this runs once
// per slot per UE, not per RE.
static int catb_ue_slot(uint16_t rnti)
{
  static uint16_t seen[CATB_MAX_UE];
  static int n = 0;
  if (rnti == 0)
    return -1;
  for (int i = 0; i < n; i++)
    if (seen[i] == rnti)
      return i;
  if (n >= CATB_MAX_UE)
    return -1;
  seen[n] = rnti;
  return n++;
}

// ---- Cat-B 3a.3 DU side: receive already-combined data symbols -----------------------------
// The RU combines 16 antennas into ONE stream on DATA symbols (nr-oru.c receive_pusch_catb) and
// leaves REFERENCE symbols per-antenna so the DU can still estimate H and keep computing weights.
// So on data symbols the DU has y = w^H x on eAxC 0 only, while H is 16x1 from the reference
// symbols. The fix needs NO extra fronthaul: the DU already knows w because it published it.
// Build the effective 1-antenna channel locally,  h_eff = sum_a conj(w[a]) * H[a],  and run the
// existing receiver with nb_rx_ant == 1 for that symbol.
static int catb_ul_rx_enabled(void)
{
  static int en = -1;
  if (en < 0) {
    const char *e = getenv("OAI_CATB_UL_RX");
    en = (e && e[0] && e[0] != '0') ? 1 : 0;
    if (en)
      LOG_A(PHY, "[CATB DU] combined-UL receive mode ON (data symbols = 1 effective antenna)\n");
  }
  return en;
}

// Read back the weights that were APPLIED TO THIS SLOT, recorded by oaioran.c catb_bfw_attach()
// at C-plane build time. NOT the newest record: the RU combined slot N with the vector carried by
// slot N's C-plane section, which the DU built some slots earlier. Equalising with a different
// vintage leaves a residual complex scalar (mostly phase) that destroys high-order QAM.
// Returns antenna count, 0 if no BFW went out for this slot (caller then assumes the degenerate
// weight — unit on antenna 0 — which is exactly what the RU sends in that case).
static catb_weight_ring_t *catb_get_ring(void);

static int catb_read_applied_weights(int slot, int16_t *out, int max_ant)
{
  // Use the EXPORT path's handle. This used to map a second, private handle with a
  // `(tries++ % 2000) != 0` backoff, which deadlocked (§21): the first attempt happens before the
  // ring exists and the next is at call 2000, but this function is only reached a few hundred
  // times per run when nothing is decoding — so it returned 0 for entire runs. The DU then sat on
  // its antenna-0 fallback while the RU combined with real weights, and no TB could decode.
  // MEASURED: |h_eff|/|H_ant0| = 0.991 (the fallback identity) with n_w=0 on every sample.
  // Same process, same ring — a second mapping was never needed.
  catb_weight_ring_t *ring = catb_get_ring();
  if (ring == NULL)
    return 0;
  const int n = catb_applied_read(ring, slot, out, max_ant);
  static long n_call = 0, n_hit = 0;
  n_call++;
  if (n > 0)
    n_hit++;
  if ((n_call % 2000) == 1)
    LOG_A(PHY, "[CATB DU] applied_read hit/call = %ld/%ld\n", n_hit, n_call);
  return n;
}

// Shared export gate + ring handle. STEP 2 published weights as a passive by-product of the
// MU-IRC receiver, which was free while the DU was a 16-antenna receiver on every symbol. STEP 3
// breaks that: once the RU combines, the DU runs at 1 effective antenna on DATA symbols, the IRC
// path stops engaging (g_mu_mimo_active goes 0 once throughput collapses), and weight production
// dies — which starves the combining that caused it. Weight computation therefore has to be a
// first-class step on REFERENCE symbols, not a side effect of decoding.
static catb_weight_ring_t *catb_get_ring(void)
{
  static int en = -1;
  static catb_weight_ring_t *ring = NULL;
  if (en < 0) {
    const char *e = getenv("OAI_CATB_WEIGHT_EXPORT");
    en = (e && e[0] && e[0] != '0') ? 1 : 0;
    if (en) {
      ring = catb_ring_open(1);
      LOG_A(PHY, "[CATB] weight export %s\n", ring ? "ON" : "FAILED to map ring");
    }
  }
  return en ? ring : NULL;
}

// Must match nr-oru.c catb_is_ref_symbol() and VRTSIM_CATB_REF_SYMS. Default 2,7,11.
static int catb_du_is_ref_symbol(int symbol)
{
  static int inited = 0;
  static uint16_t mask = 0;
  if (!inited) {
    inited = 1;
    const char *e = getenv("VRTSIM_CATB_REF_SYMS");
    if (e && e[0]) {
      const char *p = e;
      while (*p) {
        int v = atoi(p);
        if (v >= 0 && v < 14)
          mask |= (uint16_t)(1u << v);
        while (*p && *p != ',')
          p++;
        if (*p == ',')
          p++;
      }
    }
    if (mask == 0)
      mask = (1u << 2) | (1u << 7) | (1u << 11);
    LOG_A(PHY, "[CATB DU] reference symbol mask 0x%04x\n", mask);
  }
  return (mask >> symbol) & 1;
}

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

  c16_t af_mf_00[12 * nb_rb] __attribute__((aligned(32)));
  c16_t af_mf_01[12 * nb_rb] __attribute__((aligned(32)));
  c16_t af_mf_10[12 * nb_rb] __attribute__((aligned(32)));
  c16_t af_mf_11[12 * nb_rb] __attribute__((aligned(32)));
  uint32_t determ_fin[12*nb_rb] __attribute__((aligned(32)));

  /* H is [nb_rx_ant x 2] (layer 0 = self, layer 1 = partner); H^H*H is ALWAYS 2x2:
   *   af_mf_XY[re] = sum_a conj(H[a][X]) * H[a][Y]   (saturating int16 accumulation)
   * Generic over antenna count — replaces the hand-unrolled nb_rx_ant==2/4 switch, which
   * returned -1 for 8/16 and blocked the antenna ladder. */
  // Wide-accumulator Gram matrix H^H*H: sum each conj-product across all antennas in int32
  // lanes, apply one rounded shift after the full sum, saturating pack to int16. The old path
  // (mult_cpx_conj_vector per antenna -> per-antenna shift, then saturating int16 adds_epi16)
  // lost low-order bits and could wrap at 16 antennas, detuning the 2x2 inversion -> the
  // joint-kernel MCS ceiling. Same fix as the single-stream MRC accumulator. Replicates
  // oai_mm_cpx_mult_conj's two madd ops (re=madd(a,b), im=madd(swap(conj(a)),b)) exactly.
  c16_t *const afmf[4] = {af_mf_00, af_mf_01, af_mf_10, af_mf_11};
  const simde__m128i roundv = simde_mm_set1_epi32(shift > 0 ? 1 << (shift - 1) : 0);
  for (int e = 0; e < 4; e++) {
    const int lx = e >> 1, ly = e & 1; // entries (0,0)(0,1)(1,0)(1,1)
    simde__m128i *dst = (simde__m128i *)afmf[e];
    for (uint32_t k = 0; k < 3 * nb_rb_0; k++) {
      simde__m128i acc_re = simde_mm_setzero_si128();
      simde__m128i acc_im = simde_mm_setzero_si128();
      for (int aa = 0; aa < nb_rx_ant; aa++) {
        const simde__m128i a = ((simde__m128i *)ul_ch_estimates_ext[lx][aa])[k];
        const simde__m128i b = ((simde__m128i *)ul_ch_estimates_ext[ly][aa])[k];
        acc_re = simde_mm_add_epi32(acc_re, simde_mm_madd_epi16(a, b));
        acc_im = simde_mm_add_epi32(acc_im, simde_mm_madd_epi16(oai_mm_swap(oai_mm_conj(a)), b));
      }
      acc_re = simde_mm_srai_epi32(simde_mm_add_epi32(acc_re, roundv), shift);
      acc_im = simde_mm_srai_epi32(simde_mm_add_epi32(acc_im, roundv), shift);
      dst[k] = oai_mm_pack(acc_re, acc_im);
    }
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
  const int nb_rx_ant_true = frame_parms->nb_antennas_rx;
  // On Cat-B DATA symbols the wire carries ONE combined stream, so the receiver runs at 1
  // antenna. Every VLA below and every downstream call is sized from nb_rx_ant, so switching it
  // here keeps the whole path self-consistent. ul_ch_estimates[] is still stored per REAL
  // antenna, so its indexing must use nb_rx_ant_true.
  // A symbol is a REFERENCE symbol iff it carries DMRS — PHY knows this natively, so there is no
  // reason to guess a symbol list. Publish the bitmap for the fronthaul layer, which cannot see it.
  {
    catb_weight_ring_t *r = catb_get_ring();
    if (r != NULL && r->dmrs_mask != (uint16_t)rel15_ul->ul_dmrs_symb_pos) {
      r->dmrs_mask = (uint16_t)rel15_ul->ul_dmrs_symb_pos;
      LOG_A(PHY, "[CATB] DMRS symbol mask published 0x%04x\n", (unsigned)r->dmrs_mask);
    }
  }
  const int catb_dmrs_sym = (rel15_ul->ul_dmrs_symb_pos >> symbol) & 1;
  const int catb_comb = catb_ul_rx_enabled() && !catb_dmrs_sym;
  int nb_rx_ant = catb_comb ? 1 : nb_rx_ant_true;
  // ---- Cat-B STEP 4 CORE: WEIGHT STALENESS at the SOURCE (OAI_CATB_DELAY_SLOTS=d) --------
  // Delay the CHANNEL ESTIMATE itself, before anything reads it. An earlier version swapped
  // only the MMSE receiver's copy (chF2) and produced a flat ~98% collapse at EVERY delay:
  // the data had been compensated with the FRESH channel while the Gram matrix came from the
  // stale one, so the decoder inverted a channel that did not match its own input. That
  // mismatch is total regardless of age, which is why it did not vary with coherence time.
  // Substituting here keeps extraction, compensation and the Gram matrix mutually consistent
  // — all of them see one channel, just an older one. That IS the fronthaul loop's effect.
  // d is in SLOTS: at TS=0.02 a real 100 us FH latency is 2 us of sim time, invisible.
  {
    static int cdelay = -1;
    if (cdelay < 0) {
      const char *e = getenv("OAI_CATB_DELAY_SLOTS");
      cdelay = (e && e[0]) ? atoi(e) : 0;
      if (cdelay > 0)
        LOG_A(PHY, "[CATB] weight staleness %d slots (%.1f ms sim), applied at the estimate\n", cdelay, cdelay * 0.5);
    }
    if (cdelay > 0 && symbol == rel15_ul->start_symbol_index) { // once per slot, not per symbol
      // Ring depth must be d+1, not 64. At 64 this allocated ~114 kB per antenna per slot
      // x16 antennas x64 slots x2 UEs ~= 234 MB of malloc inside the real-time decode path,
      // with ~1.8 MB of memcpy per slot per UE on top. That starves the slot deadline whatever
      // the contents are — which is exactly the "destructive whenever active" signature seen
      // on a static channel, where the substituted data is byte-identical to the live data.
      const int CR = (cdelay < 63) ? (cdelay + 1) : 64;
      enum { CU = CATB_MAX_UE, CRMAX = 64 };
      const size_t one = sizeof(int32_t) * frame_parms->ofdm_symbol_size * frame_parms->symbols_per_slot;
      // Key by the decode's real identity (RNTI). Unknown/overflow => skip, so the mechanism
      // degrades to "no delay" instead of silently mixing UEs.
      const int ue = catb_ue_slot(rel15_ul->rnti);
      if (ue < 0)
        goto catb_ring_done;
      const int d = (cdelay < CR) ? cdelay : CR - 1;
      static int32_t *ring[CU][CRMAX][16];
      static int pos[CU], filled[CU];
      const int wr = pos[ue] % CR;
      for (int a = 0; a < nb_rx_ant && a < 16; a++) {
        if (!ring[ue][wr][a])
          ring[ue][wr][a] = malloc(one);
        if (ring[ue][wr][a])
          memcpy(ring[ue][wr][a], pusch_vars->ul_ch_estimates[a], one);
      }
      pos[ue]++;
      if (filled[ue] < CR)
        filled[ue]++;
      // DO NOT write back into pusch_vars->ul_ch_estimates. OAI decodes symbols in parallel
      // tasks that read that buffer concurrently, so mutating it races with live readers —
      // which is why substitution destroyed decoding even on a STATIC channel, where the old
      // and new contents are byte-identical. Publish read-only pointers instead; consumers
      // below take the delayed view without anyone's shared state being modified.
      if (filled[ue] > d) {
        const int rd = ((pos[ue] - 1 - d) % CR + CR) % CR;
        for (int a = 0; a < nb_rx_ant && a < 16; a++)
          catb_hview[ue][a] = ring[ue][rd][a];
      }
catb_ring_done:;
    }
  }
  // ----------------------------------------------------------------------------------------
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

  // ---- Cat-B STEP 3: weight computation on REFERENCE symbols -------------------------------
  // Independent of the MU-IRC decode path: no g_mu_mimo_active, no log2_maxh, no dependence on
  // the data-symbol receive mode. Runs where nb_rx_ant is still the true antenna count, which is
  // the only place the DU still sees an uncombined 16-antenna view.
  // EXIT-PATH CENSUS (§20). Weight production runs ~once per RUN while the RU consumes 26001
  // sections, but the counters inside the block cannot say WHICH gate rejects — [CATB WDIAG] sits
  // inside `wpart >= 0`, so its silence is ambiguous. Count every exit, exactly as the [CATB
  // ATTACH] census did for §18/§19. Log counts, never a presence flag.
  static long p_call = 0, p_comb = 0, p_notdmrs = 0, p_ant = 0, p_ring = 0, p_dup = 0,
              p_nopart = 0, p_buf = 0, p_lowsum = 0, p_pub = 0, p_pub_su = 0;
  p_call++;
  if ((p_call % 20000) == 0)
    LOG_A(PHY,
          "[CATB PROD] calls=%ld comb=%ld notdmrs=%ld antmix=%ld ring=%ld dup=%ld nopartner=%ld"
          " buf=%ld lowsum=%ld PUB_MU=%ld PUB_SU=%ld\n",
          p_call, p_comb, p_notdmrs, p_ant, p_ring, p_dup, p_nopart, p_buf, p_lowsum, p_pub, p_pub_su);
  if (catb_comb)
    p_comb++;
  else if (!catb_dmrs_sym)
    p_notdmrs++;
  else if (nb_rx_ant != nb_rx_ant_true)
    p_ant++;
  if (!catb_comb && catb_dmrs_sym && nb_rx_ant == nb_rx_ant_true) {
    catb_weight_ring_t *ring = catb_get_ring();
    static uint32_t pub_f = 0xffffffffu;
    static int pub_s = -1;
    static uint16_t pub_r = 0;
    if (ring == NULL)
      p_ring++;
    else if (!(frame != pub_f || slot != pub_s || rel15_ul->rnti != pub_r))
      p_dup++;
    if (ring != NULL && (frame != pub_f || slot != pub_s || rel15_ul->rnti != pub_r)) {
      // Partner scan: same PRB match + per-slot chest stamp as the IRC scan, but WITHOUT the
      // MU regime gate. We only need both UEs' channel estimates to exist for this slot.
      int wpart = -1;
      for (int id = 0; id < gNB->max_nb_pusch; id++) {
        if (id == ulsch_id)
          continue;
        const NR_gNB_ULSCH_t *u = &gNB->ulsch[id];
        if (u->harq_process == NULL)
          continue;
        if (gNB->pusch_vars[id].mu_chest_frame != (int)frame || gNB->pusch_vars[id].mu_chest_slot != slot)
          continue;
        const nfapi_nr_pusch_pdu_t *p = &u->harq_process->ulsch_pdu;
        if (p->rb_size != rel15_ul->rb_size || p->rb_start != rel15_ul->rb_start)
          continue;
        if (p->rnti == rel15_ul->rnti)
          continue;
        wpart = id;
        break;
      }
      if (wpart < 0)
        p_nopart++;
      // MU-ONLY BY DEFAULT (§23). The SU fallback is opt-in via OAI_CATB_SU_BFW=1.
      // Why it is OFF: the census that motivated it (nopartner=4317 vs PUBLISHED=114) was taken
      // while §22's bug was live — the DU's ring was unmapped, so NOTHING decoded, so the
      // scheduler stopped co-scheduling, so the partner scan failed. Cause and effect were
      // inverted: the "production deadlock" was a SYMPTOM of §22, not an independent deadlock.
      // And SU is harmful here: one wideband beam serves one UE, so it nulls the co-scheduled UE
      // and kills its Msg3 (attach 2/2 -> 1/2, reproducible 3/3, independent of weight quality).
      // The bootstrap needs no SU rung: with no weights the RU forwards all 16 antennas and the DU
      // decodes per-antenna, which is how runs 1-3 reached attach 2/2. MU with identical PRBs is
      // the configuration the 2x2 MMSE was built for — two layers, each nulling the other.
      static int su_en = -1;
      if (su_en < 0) {
        const char *e = getenv("OAI_CATB_SU_BFW");
        su_en = (e && e[0] && e[0] != '0') ? 1 : 0;
        LOG_A(PHY, "[CATB] SU weight fallback %s\n", su_en ? "ON" : "OFF (MU only)");
      }
      if (wpart < 0 && !su_en) {
        // No partner and SU disabled: publish nothing. The RU then sees no weights and forwards
        // every antenna, which the DU's fallback already assumes — the two sides stay consistent.
      } else
      // SU FALLBACK (§20). MEASURED: the partner scan fails 97.4% of reachable calls (nopartner
      // 4317 vs PUBLISHED 114), because weights -> decode -> throughput -> co-scheduling -> weights
      // is a cycle. Once coverage reached 100% the data path collapsed, the scheduler stopped
      // pairing the UEs, and production stopped for the rest of the run — 3.1 -> 0.0 Mbps.
      // Publishing SINGLE-USER weights when there is no partner breaks the cycle.
      //
      // This needs NO change to catb_publish_weights: with the partner's channel ZEROED, its 2x2
      // MMSE reduces algebraically to the correct one-user solution. G becomes diag(|h0|^2+nvar,
      // nvar), so det = (|h0|^2+nvar)*nvar != 0 (not singular), invG[0][1] = 0, and
      // w[0][a] = (nvar/det)*conj(h0[a]) — the matched filter, up to a real scale that cancels
      // (§17 bug 6: only the DIRECTION of w matters). w[1] comes out zero and is never emitted
      // anyway, since oaioran.c takes layer 0. Deriving the SU weights from the SAME expression
      // also inherits its conjugation convention, rather than re-guessing it — re-guessing is what
      // produced §15's residual phase rotation.
      {
        // Heap-backed via a TLS pointer: 2 x 16 x buffer_length x 4 B is ~160 kB, which belongs
        // on neither the decode-thread stack nor the static TLS block (learned in nr-oru.c).
        enum { CW_L = 2, CW_ANT = 16, CW_MAXRE = 4096 };
        static __thread c16_t *cw_buf = NULL;
        if (cw_buf == NULL)
          cw_buf = aligned_alloc(32, (size_t)(CW_L * CW_ANT + 1) * CW_MAXRE * sizeof(c16_t));
        if (!(cw_buf != NULL && buffer_length <= CW_MAXRE && nb_rx_ant_true <= CW_ANT))
          p_buf++;
        if (cw_buf != NULL && buffer_length <= CW_MAXRE && nb_rx_ant_true <= CW_ANT) {
          c16_t (*ch2)[CW_ANT][CW_MAXRE] = (c16_t (*)[CW_ANT][CW_MAXRE])cw_buf;
          c16_t *dump = cw_buf + (size_t)CW_L * CW_ANT * CW_MAXRE;
          NR_gNB_PUSCH *pv_w = (wpart >= 0) ? &gNB->pusch_vars[wpart] : NULL;
          for (int a = 0; a < nb_rx_ant_true; a++) {
            nr_ulsch_extract_rbs(rxF[a], (c16_t *)pusch_vars->ul_ch_estimates[a], dump, ch2[0][a],
                                 soffset + (symbol * frame_parms->ofdm_symbol_size),
                                 dmrs_symbol * frame_parms->ofdm_symbol_size, a, dmrs_symbol_flag,
                                 rel15_ul, frame_parms);
            if (pv_w != NULL)
              nr_ulsch_extract_rbs(rxF[a], (c16_t *)pv_w->ul_ch_estimates[a], dump, ch2[1][a],
                                   soffset + (symbol * frame_parms->ofdm_symbol_size),
                                   dmrs_symbol * frame_parms->ofdm_symbol_size, a, dmrs_symbol_flag,
                                   rel15_ul, frame_parms);
            else
              memset(ch2[1][a], 0, (size_t)CW_MAXRE * sizeof(c16_t));
          }
          // Is the channel we are about to publish from actually populated? A zeroed channel makes
          // G = H^H H + nvar I singular, publish_weights leaves the PRB at zero, and an ALL-ZERO
          // weight vector goes on the wire (observed: w[0..3]=(0,0)(0,0)). MEASURE it — three
          // inferred causes for that symptom were all wrong.
          long s0sum = 0, s1sum = 0;
          const int rpp = dmrs_symbol_flag ? 6 : 12;
          for (int a = 0; a < nb_rx_ant_true; a++)
            for (int k = 0; k < rel15_ul->rb_size * rpp && k < CW_MAXRE; k++) {
              s0sum += abs(ch2[0][a][k].r) + abs(ch2[0][a][k].i);
              s1sum += abs(ch2[1][a][k].r) + abs(ch2[1][a][k].i);
            }
          static long ndiag = 0;
          if ((ndiag++ % 500) == 0)
            LOG_A(PHY,
                  "[CATB WDIAG] sym=%d dmrs=%d rpp=%d self_sum=%ld partner_sum=%ld "
                  "(zero => estimate not ready on this symbol)\n",
                  symbol, dmrs_symbol_flag, rpp, s0sum, s1sum);
          // Publish only from a symbol whose channel is real; otherwise leave the slot UNMARKED so
          // a LATER reference symbol in the same slot gets its turn. nb_rx_ant==16 holds only on
          // reference symbols, but ul_ch_estimates is populated only after the first DMRS symbol
          // is processed — the two windows overlap on the SECOND and later reference symbols.
          // In SU mode s1sum is zero BY CONSTRUCTION, so it must not veto the publish. Only the
          // channel actually being beamformed has to be real.
          const int sums_ok = (s0sum > 1000) && (pv_w == NULL || s1sum > 1000);
          if (!sums_ok)
            p_lowsum++;
          if (sums_ok) { // 8 is noise; a real channel is ~1e5
            const int n_sing =
                catb_publish_weights(ring, (int)frame, slot, rel15_ul->rnti, rel15_ul->rb_start,
                                     rel15_ul->rb_size, nb_rx_ant_true, CW_MAXRE,
                                     (const c16_t (*)[nb_rx_ant_true][CW_MAXRE])ch2, nvar, rpp, 1);
            pub_f = frame; pub_s = slot; pub_r = rel15_ul->rnti;
            if (pv_w != NULL)
              p_pub++;
            else
              p_pub_su++;
            static long npub = 0;
            if ((npub++ % 500) == 0)
              // nvar is logged because the SU determinant is PROPORTIONAL to it: nvar==0 zeroes
              // every PRB (§20). w0 is logged because "published" is a presence flag and presence
              // flags have lied here before — an all-zero vector publishes just as happily.
              LOG_A(PHY, "[CATB] ref-symbol weight publish #%ld (sym %d, partner ulsch %d, "
                         "self_sum=%ld partner_sum=%ld nvar=%u singular_prb=%d/%d)\n",
                    npub, symbol, wpart, s0sum, s1sum, nvar, n_sing, rel15_ul->rb_size);
          }
        }
      }
    }
  }

  if (catb_comb) {
    // Effective 1-antenna receive. rxF[0] holds the RU-combined stream; chFext[l][0] becomes
    // h_eff = sum_a conj(w[a]) * H[l][a], using the weights this DU published for this slot.
    int16_t w[CATB_MAX_ANT * 2];
    const int n_w = catb_read_applied_weights(slot, w, nb_rx_ant_true);
    c16_t tmp[buffer_length] __attribute__((aligned(32)));
    c16_t dump[buffer_length] __attribute__((aligned(32)));
    // PROBE B control (§21): antenna 0's channel measured in the SAME post-extraction domain as
    // h_eff. Reading ul_ch_estimates directly was wrong — that is the raw frequency-domain buffer
    // and its index i means something different, so the control read zeros.
    long mh0 = 0;
    for (int aatx = 0; aatx < nb_layer; aatx++) {
      int32_t acc_r[buffer_length], acc_i[buffer_length];
      memset(acc_r, 0, sizeof(acc_r));
      memset(acc_i, 0, sizeof(acc_i));
      for (int a = 0; a < nb_rx_ant_true; a++) {
        // Extract this antenna's channel estimate. rxFext is written only on a == 0 (the
        // combined stream); later antennas dump theirs, we only want their channel.
        nr_ulsch_extract_rbs(rxF[0],
                             (c16_t *)pusch_vars->ul_ch_estimates[aatx * nb_rx_ant_true + a],
                             (a == 0) ? rxFext[0] : dump,
                             tmp,
                             soffset + (symbol * frame_parms->ofdm_symbol_size),
                             dmrs_symbol * frame_parms->ofdm_symbol_size,
                             0,
                             dmrs_symbol_flag,
                             rel15_ul,
                             frame_parms);
        // No weights yet -> fall back to plain MRC combining weights (conj(H) applied later),
        // i.e. treat w as unit on antenna 0 only. Counted below so it is never silent.
        const int32_t wr = (n_w > a) ? w[2 * a] : ((a == 0) ? 32767 : 0);
        const int32_t wi = (n_w > a) ? w[2 * a + 1] : 0;
        if (aatx == 0 && a == 0)
          for (int i = 0; i < (int)buffer_length; i++)
            mh0 += abs(tmp[i].r) + abs(tmp[i].i);
        // ACCUMULATE FULL PRECISION, SHIFT ONCE (§24). The >>15 used to sit INSIDE this loop, so
        // every one of the 16 antenna terms was truncated to an integer before being summed:
        // with |tmp| ~ 137/RE and |w_a| ~ 2048, each term is 137*2048 >> 15 = 8.5 -> 8. Sixteen
        // roundings compound, and h_eff came out at 0.22-0.36 of |H_ant0| where a coherent sum
        // should approach 1. No overflow risk: w is L1-normalised (sum_a |w_a| <= 32768), so the
        // accumulated magnitude is bounded by 32767 * 32768 = 1.07e9, inside int32.
        for (int i = 0; i < (int)buffer_length; i++) {
          // NO SECOND CONJUGATE (§25) — must match the RU's combine exactly. W = G^-1 H^H already
          // carries the conjugate, so h_eff = sum_a w_a H_a, not sum_a conj(w_a) H_a.
          acc_r[i] += (int32_t)tmp[i].r * wr - (int32_t)tmp[i].i * wi;
          acc_i[i] += (int32_t)tmp[i].r * wi + (int32_t)tmp[i].i * wr;
        }
      }
      for (int i = 0; i < (int)buffer_length; i++) {
        const int32_t vr = acc_r[i] >> 15;
        const int32_t vi = acc_i[i] >> 15;
        chFext[aatx][0][i].r = (int16_t)(vr > 32767 ? 32767 : (vr < -32768 ? -32768 : vr));
        chFext[aatx][0][i].i = (int16_t)(vi > 32767 ? 32767 : (vi < -32768 ? -32768 : vi));
      }
    }
    static long n_eff = 0, n_now = 0;
    if (n_w <= 0)
      n_now++;
    if ((n_eff++ % 20000) == 0)
      LOG_A(PHY, "[CATB DU] effective-channel symbols=%ld no_weights=%ld\n", n_eff, n_now);
    // PROBE B (§21). Did the combined signal survive the fronthaul, and is h_eff non-degenerate?
    // |rxFext| is the received combined stream; |chFext| is h_eff. The DU decodes this as pure
    // noise (pwr == npwr, llr 0), so one of the two is collapsed. Compared against |ul_ch_estimates
    // on antenna 0|, which is measured on UNCOMBINED reference symbols and is known-good — a
    // control in the same units, since a lone magnitude has no scale to be judged against.
    {
      long mrx = 0, mheff = 0;
      for (int i = 0; i < (int)buffer_length; i++) {
        mrx += abs(rxFext[0][i].r) + abs(rxFext[0][i].i);
        mheff += abs(chFext[0][0][i].r) + abs(chFext[0][0][i].i);
      }
      // Only symbols carrying signal, and often — the previous cadence (every 20000) fired ONCE in
      // a whole run, and on an n_w=0 fallback symbol, so it measured the wrong path entirely.
      static long nb = 0;
      if (mrx > 0 && (nb++ % 200) == 0)
        LOG_A(PHY,
              "[CATB MAG-B] slot=%d sym=%d n_w=%d len=%u |rx_combined|=%ld |h_eff|=%ld |H_ant0|=%ld"
              " heff/H=%.3f w[0..3]=(%d,%d)(%d,%d)(%d,%d)(%d,%d)\n",
              slot, symbol, n_w, buffer_length, mrx, mheff, mh0,
              mh0 ? (double)mheff / (double)mh0 : -1.0,
              w[0], w[1], w[2], w[3], w[4], w[5], w[6], w[7]);
    }
  } else
  for (int aarx = 0; aarx < nb_rx_ant; aarx++) {
    for (int aatx = 0; aatx < nb_layer; aatx++) {
      // STEP 4: the SELF leg must be delayed too. W = f(H_self, H_partner), so delaying only the
      // partner's estimate (which is all the IRC block below did) models half the staleness and
      // the equivalence "delaying H == delaying W" does not hold. The ring stores layer 0's
      // nb_rx_ant estimates, so this substitution is valid for the 1-layer-per-UE case this
      // experiment runs; higher nb_layer falls through to the live estimate.
      const int self_slot = (aatx == 0) ? catb_ue_slot(rel15_ul->rnti) : -1;
      int32_t *self_hv = (self_slot >= 0) ? catb_hview[self_slot][aarx] : NULL;
      nr_ulsch_extract_rbs(rxF[aarx],
                           self_hv ? (c16_t *)self_hv
                                   : (c16_t *)pusch_vars->ul_ch_estimates[aatx * nb_rx_ant_true + aarx],
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
    if (mu_irc < 0) { const char *e = getenv("OAI_UL_MU_IRC"); mu_irc = (e && e[0]) ? atoi(e) : 0; }
    // MU regime gate (set by the MAC scheduler): only true when >=2 UEs are fully connected and
    // nobody is in RA. Without this, full-band Msg3 (rb 0+273) passes the rb_size filter below and
    // IRC corrupts RA -> endless PRACH-retry storm. This is the scheduler->PHY co-sched marker.
    extern volatile int g_mu_mimo_active;
    int partner = -1;
    uint16_t partner_rnti = 0;
    // Only fire on genuine co-scheduled DATA: MU regime active, large allocation, distinct rnti,
    // matching PRBs. Prevents the IRC mis-pairing during attach that breaks decode.
    // nb_rx_ant >= 2 REQUIRED: the 2-layer joint detector is degenerate at 1 antenna (can't
    // separate 2 co-channel UEs with 1 RX) -> it corrupts EVERY decode (grants carry garbage,
    // both UEs starve). At 1 antenna the co-scheduled UEs simply collide; IRC cannot help.
    // "Large allocation" must be relative to the CARRIER, not an absolute PRB count: the old
    // literal 137 was derived as half of a 273-PRB carrier, so at any narrower bandwidth every
    // grant fell below it and IRC never engaged — the two co-scheduled UEs then collided on the
    // same PRBs with no spatial separation (measured at 106 PRB: zero [MU METRIC] lines,
    // MCS 17-24 and ~17% BLER instead of MCS 28). Half the carrier keeps the original 273-PRB
    // behaviour (137 ~= 273/2) while still excluding small allocations such as Msg3.
    const int mu_min_rb = frame_parms->N_RB_UL >> 1;
    { static int dg = 0; if (mu_irc && !dmrs_symbol_flag && rel15_ul->rb_size > mu_min_rb && dg++ < 6)
        LOG_E(PHY, "[MU GATE] nb_rx_ant=%d g_mu=%d nb_layer=%d rb_size=%d min_rb=%d max_pusch=%d\n",
              nb_rx_ant, g_mu_mimo_active, nb_layer, rel15_ul->rb_size, mu_min_rb, gNB->max_nb_pusch); }
    if (mu_irc && g_mu_mimo_active && pusch_vars->log2_maxh > 0 && nb_rx_ant >= 2 && nb_layer == 1 && rel15_ul->rb_size > mu_min_rb) {
      // CRASH FIX (was Block-1): gNB->ulsch[id].harq_process is NULL for unused slots -> the old
      // unguarded ->ulsch_pdu deref segfaulted (at 0) the first time the scan ran past the active
      // ids (dmesg: Tpool segfault at 0, du.log dead right after first [MU METRIC] in EVERY run —
      // the "instability/churn" was the DU dying). Guard active+harq_process, and require the
      // partner to be scheduled THIS SAME frame/slot (kills the stale-partner mispairing too).
      const NR_gNB_ULSCH_t *cur = &gNB->ulsch[ulsch_id];
      for (int id = 0; id < gNB->max_nb_pusch; id++) {
        if (id == ulsch_id) continue;
        const NR_gNB_ULSCH_t *u = &gNB->ulsch[id];
        // NOT gated on u->active: the partner clears active the moment ITS decode completes, which
        // stripped IRC from the slower UE's tail symbols mid-slot (MRC + co-channel interference =
        // garbage LLRs = every co-channel TB dead at high rho). frame/slot match already rejects
        // stale entries; estimates/pdu persist for the slot after completion.
        // Partner-scan diagnosis: report WHICH condition rejects each candidate. Without this the
        // only symptom is a silent absence of [MU METRIC] and a fallback to plain MRC.
        static long ps_null = 0, ps_fs = 0, ps_rb = 0, ps_rnti = 0, ps_hit = 0, ps_log = 0;
        if (u->harq_process == NULL) { ps_null++; continue; }
        // Identify the partner by the per-slot CHANNEL-ESTIMATE stamp, not by ulsch[].frame/slot.
        // ulsch[] carries the entry's LIVE scheduling state, which the scheduler advances to that
        // UE's NEXT grant before the current slot's decode runs — at 106 PRB the partner's entry
        // read 250.2 while we were decoding 249.12, so strict equality rejected every genuine
        // partner (measured: null=0 rb=0 rnti=0 hit=0, all rejections frame/slot) and IRC never
        // engaged even though the MAC reported 2216 same-PRB reuse slots. The chest stamp is
        // written when THIS slot's estimate is produced and is exactly what the IRC consumes.
        if (gNB->pusch_vars[id].mu_chest_frame != (int)frame || gNB->pusch_vars[id].mu_chest_slot != slot) {
          ps_fs++;
          if ((ps_log++ % 2000) == 0)
            LOG_E(PHY, "[MU PSCAN] id=%d rejected chest-stamp: cand %d.%d vs cur %d.%d (ulsch %d.%d) (null=%ld fs=%ld rb=%ld rnti=%ld hit=%ld)\n",
                  id, gNB->pusch_vars[id].mu_chest_frame, gNB->pusch_vars[id].mu_chest_slot,
                  (int)frame, slot, u->frame, u->slot, ps_null, ps_fs, ps_rb, ps_rnti, ps_hit);
          continue;
        }
        const nfapi_nr_pusch_pdu_t *p = &u->harq_process->ulsch_pdu;
        if (p->rb_size != rel15_ul->rb_size || p->rb_start != rel15_ul->rb_start) {
          ps_rb++;
          if ((ps_log++ % 2000) == 0)
            LOG_E(PHY, "[MU PSCAN] id=%d rejected rb: cand %d+%d vs cur %d+%d (null=%ld fs=%ld rb=%ld rnti=%ld hit=%ld)\n",
                  id, p->rb_start, p->rb_size, rel15_ul->rb_start, rel15_ul->rb_size,
                  ps_null, ps_fs, ps_rb, ps_rnti, ps_hit);
          continue;
        }
        if (p->rnti == rel15_ul->rnti) { ps_rnti++; continue; }
        {
          partner = id;
          partner_rnti = p->rnti; // Cat-B STEP 4: RNTI is the ring key, ulsch_id is not stable
          ps_hit++;
          if ((ps_log++ % 2000) == 0)
            LOG_E(PHY, "[MU PSCAN] HIT id=%d rnti=%04x (null=%ld fs=%ld rb=%ld rnti=%ld hit=%ld)\n",
                  id, p->rnti, ps_null, ps_fs, ps_rb, ps_rnti, ps_hit);
          break;
        }
      }
    }
    // Coverage counters: a TB whose data symbols are only PARTLY IRC'd dies — any fall-through
    // symbol is decoded by plain MRC WITH co-channel interference => garbage LLRs for those REs.
    // Track why symbols fall through; printed with [MU METRIC].
    static long mu_c_irc = 0, mu_c_nopart = 0, mu_c_rxe = 0, mu_c_parte = 0, mu_c_byp = 0;
    if (mu_irc && g_mu_mimo_active && pusch_vars->log2_maxh > 0 && nb_rx_ant >= 2 && nb_layer == 1 && rel15_ul->rb_size > mu_min_rb && partner < 0)
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
    // Freshness gate: a stale partner estimate (previous slot's chest, full-strength) passes any
    // ENERGY check and the 2-layer MMSE then crushes the self stream (the per-UE all-round HARQ
    // chain state). Only use the partner if its hoisted chest is stamped with THIS frame/slot.
    if (partner >= 0
        && !(gNB->pusch_vars[partner].mu_chest_frame == (int)frame && gNB->pusch_vars[partner].mu_chest_slot == slot)) {
      partner = -1;
      mu_c_parte++;
    }
    // Coverage census, printed independently of [MU METRIC] (which only fires once the IRC path
    // is actually taken) so a partner that is FOUND but then dropped downstream is still visible.
    { static long cv = 0;
      if (mu_irc && g_mu_mimo_active && (cv++ % 20000) == 0)
        LOG_E(PHY, "[MU COV] irc=%ld nopart=%ld rxe=%ld parte=%ld byp=%ld partner=%d\n",
              mu_c_irc, mu_c_nopart, mu_c_rxe, mu_c_parte, mu_c_byp, partner); }
    if (partner >= 0) {
      NR_gNB_PUSCH *pv_p = &gNB->pusch_vars[partner];
      c16_t chF2[2][nb_rx_ant][buffer_length] __attribute__((aligned(32)));
      c16_t dummy[buffer_length] __attribute__((aligned(32)));
      memset(chF2, 0, sizeof(chF2));
      // REUSE the normal extraction: rxFext (received) + chFext[0] (self channel) are already
      // filled above. Only extract the PARTNER channel into chF2[1]. chF2[0] = self.
      for (int aarx = 0; aarx < nb_rx_ant; aarx++) {
        memcpy(chF2[0][aarx], chFext[0][aarx], buffer_length * sizeof(c16_t));
        // Keyed by PARTNER, not self: this substitutes pv_p->ul_ch_estimates, which belongs to
        // the partner. Keying by ulsch_id fed UE A's channel in where UE B's belonged — a
        // cross-UE swap wearing a delay's clothing, and one that PASSES a "view != nil" gate.
        // Keyed by the PARTNER's RNTI: this substitutes pv_p->ul_ch_estimates, which is the
        // partner's channel. Keying by self fed UE A's channel where UE B's belonged — a cross-UE
        // swap that still PASSES a "view != nil" gate.
        const int p_slot = catb_ue_slot(partner_rnti);
        { int32_t *hv = (p_slot >= 0) ? catb_hview[p_slot][aarx] : NULL;
          nr_ulsch_extract_rbs(rxF[aarx], (c16_t *)(hv ? hv : pv_p->ul_ch_estimates[aarx]), dummy, chF2[1][aarx],
                             soffset + (symbol * frame_parms->ofdm_symbol_size), dmrs_symbol * frame_parms->ofdm_symbol_size,
                             aarx, dmrs_symbol_flag, rel15_ul, frame_parms); }
      }
      // CATB CHECKSUM: what does the receiver actually see, delay on vs off? On a static
      // channel these MUST be identical between d=0 and d>0 — identical checksums mean the
      // redirect never lands and the damage is elsewhere in the block; differing ones locate
      // it in the substitution itself.
      {
        static long ck = 0;
        if ((ck++ % 4000) == 0) {
          long c0 = 0, c1 = 0;
          for (int i = 0; i < (int)buffer_length; i++) {
            c0 += abs(chF2[0][0][i].r) + abs(chF2[0][0][i].i);
            c1 += abs(chF2[1][0][i].r) + abs(chF2[1][0][i].i);
          }
          LOG_I(PHY, "[CATB CK] ulsch %d sym %d rb %d+%d self=%ld partner=%ld view=%p\n",
                ulsch_id, symbol, rel15_ul->rb_start, rel15_ul->rb_size, c0, c1,
                (void *)((catb_ue_slot(partner_rnti) >= 0) ? catb_hview[catb_ue_slot(partner_rnti)][0] : NULL));
        }
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
      long partE = 0, selfE = 0;
      for (int a = 0; a < nb_rx_ant; a++)
        for (int i = 0; i < 32 && i < buffer_length; i++) {
          partE += abs(chF2[1][a][i].r) + abs(chF2[1][a][i].i);
          selfE += abs(chF2[0][a][i].r) + abs(chF2[0][a][i].i);
        }
      // RELATIVE liveness: an absolute partE>=8 lets a noise-phantom partner (|chPart|~46 vs
      // |chSelf|~1700 observed) into the 2-layer MMSE, which then crushes the SELF stream
      // (out hot, llr dead, all-round HARQ chains). A real co-scheduled partner at the same AGC
      // target sits within a few dB of self; require within 15 dB (selfE>>5) else fall to MRC.
      if (partE < (selfE >> 5)) partE = 0;
      // Near-orthogonal bypass (env OAI_UL_MU_RHO_BYPASS, percent; DEFAULT 0 = OFF): when the
      // band-averaged corr2pct between the two UEs' estimates fell below the threshold, decode
      // with plain per-stream MRC instead of the joint receiver.
      //
      // DISABLED BY DEFAULT because its premise no longer holds. It was added when the joint
      // kernel had an LLR-fidelity ceiling of ~MCS 15-18, so at low correlation plain MRC was
      // genuinely the better of two bad options. That ceiling was a fixed-point defect in the
      // Gram-matrix accumulation, since fixed -- the joint kernel now decodes MCS 28 clean.
      // With that gone the bypass is strictly worse: MRC does not cancel the partner at all, so
      // it is capped at post-MF SIR ~ 1/corr (~17 dB at 2% => MCS 21-24), while IRC nulls the
      // partner and reaches MCS 28. Measured at 106 PRB, 2 UE, 16 RX, CDL-A: runs whose channel
      // draw put corr below 2% took the bypass on ~70% of symbols (byp=69211 vs irc=28158) and
      // delivered 113-119 Mbps at MCS 20-24 with ~120 retransmissions, against 177.3 Mbps at
      // MCS 28/28 with zero retransmissions when the same code path stayed on IRC (byp=0).
      // Because the trigger is the channel draw, this presented as a random per-run "dip".
      // Kept as a knob purely so the comparison can be reproduced; set it >0 to re-enable.
      static int mu_rho_byp = -1;
      if (mu_rho_byp < 0) { const char *e = getenv("OAI_UL_MU_RHO_BYPASS"); mu_rho_byp = (e && e[0]) ? atoi(e) : 0; }
      int mu_corr2 = -1;
      if (partE >= 8 && mu_rho_byp > 0) {
        double cs = 0.0; int cn = 0;
        for (int re = 0; re < buffer_length; re += 12) {
          long ipr = 0, ipi = 0, e0 = 0, e1 = 0;
          for (int a = 0; a < nb_rx_ant; a++) {
            long h0r = chF2[0][a][re].r, h0i = chF2[0][a][re].i, h1r = chF2[1][a][re].r, h1i = chF2[1][a][re].i;
            ipr += h0r*h1r + h0i*h1i; ipi += h0r*h1i - h0i*h1r; e0 += h0r*h0r + h0i*h0i; e1 += h1r*h1r + h1i*h1i;
          }
          if (e0 > 0 && e1 > 0) { cs += ((double)ipr*ipr + (double)ipi*ipi) / ((double)e0 * e1); cn++; }
        }
        mu_corr2 = cn ? (int)(100.0 * cs / cn) : -1;
      }
      const int mu_byp = (mu_corr2 >= 0 && mu_corr2 < mu_rho_byp);
      if (partE < 8) mu_c_parte++; else if (mu_byp) mu_c_byp++; else mu_c_irc++;

      if (partE >= 8 && !mu_byp) {
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
      // PRB-bundled estimate denoising: per-RE LS chest noise (~-12 dB) feeds the ML hypotheses
      // (rho/mag/MF) and floors the post-IRC SINR at ~15 dB regardless of channel quality.
      // Boxcar-average both UEs' estimates over OAI_UL_MU_CH_AVG REs (default 12 = 1 PRB, the
      // standard bundling assumption) => chest noise -10.8 dB => ML quality tracks the channel.
      { static int ch_avg = -1;
        if (ch_avg < 0) { const char *e = getenv("OAI_UL_MU_CH_AVG"); ch_avg = (e && e[0]) ? atoi(e) : 0; }
        if (ch_avg > 1) {
          for (int u = 0; u < 2; u++)
            for (int a = 0; a < nb_rx_ant; a++)
              for (int b = 0; b < buffer_length; b += ch_avg) {
                int n = (b + ch_avg <= buffer_length) ? ch_avg : buffer_length - b;
                int sr = 0, si = 0;
                for (int i = 0; i < n; i++) { sr += chF2[u][a][b + i].r; si += chF2[u][a][b + i].i; }
                c16_t m = {(int16_t)(sr / n), (int16_t)(si / n)};
                for (int i = 0; i < n; i++) chF2[u][a][b + i] = m;
              }
        } }
      nfapi_nr_pusch_pdu_t mu_pdu2 = *rel15_ul;
      mu_pdu2.nrOfLayers = 2;
      // The 1-layer log2_maxh over-attenuates the 2-layer ML demapper inputs: rho/mag land at
      // ~10-100 LSB, so the demapper's interference-hypothesis squares ((x*Q15)^2>>15) underflow
      // to 0 and cancellation floors ~10 dB regardless of SNR. Give the joint path K fewer bits
      // of right-shift (OAI's native nrOfLayers==2 formula is 3 bits hotter; headroom-safe to 5:
      // psi terms ~3*comp must stay < 32767). Env OAI_UL_MU_SHIFT_ADJ, default 3.
      static int mu_shift_adj = -1;
      if (mu_shift_adj < 0) { const char *e = getenv("OAI_UL_MU_SHIFT_ADJ"); mu_shift_adj = (e && e[0]) ? atoi(e) : 0; }
      // Qm-aware: the +K bits of compensation gain is calibrated for the QPSK LLR path (which
      // absorbs 4 bits downstream). Applying it to Qm>=4 saturates the 16/64QAM LLRs (~1300 vs
      // healthy ~75 = LDPC-fatal) -> every Qm4 TB fails -> OLLA pinned at the Qm2/4 boundary
      // (the all-round HARQ chain family). Gain only where the downstream path absorbs it.
      // Qm>=4 got 0 adj above; at MCS 27-28 the failing-TB LLR mean sits ~46 (marginal, vs
      // saturation ~1300) — OAI_UL_MU_SHIFT_ADJ_HI adds a small measured gain for Qm>=4 only.
      static int mu_shift_adj_hi = -1;
      if (mu_shift_adj_hi < 0) { const char *e = getenv("OAI_UL_MU_SHIFT_ADJ_HI"); mu_shift_adj_hi = (e && e[0]) ? atoi(e) : 0; }
      const int mu_shift_adj_eff = (rel15_ul->qam_mod_order == 2) ? mu_shift_adj : mu_shift_adj_hi;
      const int mu_shift = (output_shift > mu_shift_adj_eff) ? output_shift - mu_shift_adj_eff : 0;
      nr_ulsch_channel_compensation(buffer_length, nb_rx_ant, rxFext, chF2, mga, mgb, mgc, comp2, 2, rho2, &mu_pdu2, 0, mu_shift);
      const int mu_nre = pusch_vars->ul_valid_re_per_slot[symbol];
      // MMSE-null + standard per-stream LLR for ALL Qm — DEFAULT (OAI_UL_MU_MMSE=0 restores the
      // joint-ML kernels). The ML kernels carry an LLR-fidelity ceiling ~MCS 15-18 even on a
      // perfect channel (int16 max-log arithmetic); MMSE-IRC + per-stream LLR measured at
      // |rho|=0.588: 692 MB / MCS 22-28 vs 400 MB / MCS 14-16 for joint-ML (same window).
      static int mu_mmse = -1;
      if (mu_mmse < 0) { const char *e = getenv("OAI_UL_MU_MMSE"); mu_mmse = (e && e[0]) ? atoi(e) : 1; }
      // layer 0 = self at comp2buf[0], layer 1 = partner at comp2buf[nb_rx_ant] (rxComp[layer*nb_rx]).
      if (!mu_mmse && rel15_ul->qam_mod_order <= 6) {
        // QPSK/16/64QAM: interference-aware ML joint demapper (uses rho). llr[0]=self; scratch=partner.
        int16_t mu_llr1[buffer_length * 8] __attribute__((aligned(32)));
        nr_ulsch_compute_ML_llr(pusch_vars, symbol,
                                (c16_t *)comp2buf[0], (c16_t *)comp2buf[nb_rx_ant],
                                mga[0], mga[1], llr[0], mu_llr1,
                                rho2[0][1], rho2[1][0], mu_nre, rel15_ul->qam_mod_order);
        // renormalize: demapper LLRs are ~linear in input scale, so undo the K extra bits to
        // keep the downstream int8 LDPC input in its usual range (internal precision retained)
        if (mu_shift_adj_eff > 0) {
          int16_t *l0 = llr[0];
          for (int i = 0, nll = mu_nre * rel15_ul->qam_mod_order; i < nll; i++)
            l0[i] >>= mu_shift_adj_eff;
        }
      } else {
        // MMSE-IRC to null the partner, then per-stream LLR of the separated self.
        // symbol MUST be 0 here: comp2buf was filled by channel_compensation at offset 0 (we pass
        // symbol=0 there), and mmse_2layers indexes rxdataF_comp[.][symbol*buffer_length]. Passing
        // the real symbol made every symbol>0 read zeros/garbage and write out-of-row — the reason
        // the earlier OAI_UL_MU_MMSE=1 experiment collapsed to MCS 6.
        // Cat-B STEP 2 (passive): publish the MMSE weights this receiver uses implicitly, so the
        // RU could apply them instead. Once per (frame,slot,rnti) — the block runs per symbol and
        // the channel estimate is per slot, so exporting per symbol would be 11x redundant work in
        // the hot path for identical data. Nothing below changes; decoding is untouched.
        {
          static int catb_export = -1;
          static catb_weight_ring_t *catb_ring = NULL;
          if (catb_export < 0) {
            const char *e = getenv("OAI_CATB_WEIGHT_EXPORT");
            catb_export = (e && e[0] && e[0] != '0') ? 1 : 0;
            if (catb_export) {
              catb_ring = catb_ring_open(1);
              LOG_A(PHY, "[CATB] weight export %s\n", catb_ring ? "ON" : "FAILED to map ring");
            }
          }
          // src=2 IS OFF BY DEFAULT (§23). MEASURED, both sites in one run:
          //   src=1 (ref-symbol): WRITER nonzero=3392/3392 last=3391  -- healthy, full record
          //   src=2 (this one):   WRITER nonzero=1696/3392 last=1695, mid-PRB q=(0,0)
          // chF2 is packed at 6 REs/PRB on a DMRS symbol (extent 636) while buffer_length says
          // 1272 (12/PRB). So re = prb*12+6 exceeds the populated region from prb 53 up
          // (53*12+6 = 642 > 636) and reads ZEROS — no break, no truncation, silent garbage.
          // Both sites share one ring, so this one CLOBBERS src=1's good records: the reader's
          // cached record showed exactly last_nz=1695 and iq=(0,0) at n_prb/2, which is why the
          // combined path has produced zero weights all along.
          // §17 already moved weight production to reference symbols as a first-class step rather
          // than a side effect of decoding; this passive Step-2 exporter is vestigial. Keep it
          // reachable for A/B via OAI_CATB_PUB_IRC=1, but never let it write by default.
          static int pub_irc = -1;
          if (pub_irc < 0) {
            const char *e = getenv("OAI_CATB_PUB_IRC");
            pub_irc = (e && e[0] && e[0] != '0') ? 1 : 0;
            LOG_A(PHY, "[CATB] MU-IRC passive weight export %s\n", pub_irc ? "ON" : "OFF (ref-symbol path only)");
          }
          if (pub_irc && catb_export && catb_ring) {
            static uint32_t last_f = 0xffffffffu;
            static int last_s = -1;
            static uint16_t last_r = 0;
            if (frame != last_f || slot != last_s || rel15_ul->rnti != last_r) {
              last_f = frame; last_s = slot; last_r = rel15_ul->rnti;
              // RE STRIDE — §17 bug 5, which survived at THIS call site. A hardcoded 12 against a
              // DMRS-packed buffer (6 REs/PRB) does two things, both measured: `re = prb*12+6`
              // hits 642 >= buffer_length(636) and BREAKS at prb 53, leaving PRBs 53..105 never
              // written (last_nz=1695, and catb_bfw_attach reads n_prb/2 = 53 — the first hole);
              // and for prb < 53 it samples another PRB's RE. Derive the stride from the two
              // arguments that describe the buffer, so they cannot disagree.
              const int rpp2 = (rel15_ul->rb_size > 0) ? (int)(buffer_length / rel15_ul->rb_size) : 12;
              catb_publish_weights(catb_ring, (int)frame, slot, rel15_ul->rnti, rel15_ul->rb_start,
                                   rel15_ul->rb_size, nb_rx_ant, buffer_length,
                                   (const c16_t (*)[nb_rx_ant][buffer_length])chF2, nvar,
                                   (rpp2 == 6 || rpp2 == 12) ? rpp2 : 12, 2);
            }
          }
        }
        nr_ulsch_mmse_2layers(comp2, buffer_length, nb_rx_ant, mga, mgb, mgc, chF2, rel15_ul->rb_size,
                              rel15_ul->qam_mod_order, pusch_vars->log2_maxh, /*symbol*/ 0, mu_nre, nvar);
        nr_ulsch_compute_llr((int32_t *)comp2buf[0], mga[0], mgb[0], mgc[0], llr[0], mu_nre, symbol, rel15_ul->qam_mod_order);
      }
      // Stage-0 separation-health metric: chest magnitudes (self vs partner), post-eq output energy,
      // and LLR distribution of the separated self-stream. Diagnoses the DSP handoffs without a
      // reference: LLR~0 => extraction/scaling dead; LLR saturated => overflow; healthy+CRC-fail =>
      // residual interference (separation incomplete). Rate-limited, env OAI_UL_MU_IRC only.
      {
        static int m = 0;
        if ((m++ % 500) == 0) {  // dense: distribution per port, not just spot checks
          int nre = pusch_vars->ul_valid_re_per_slot[symbol];
          long ch0 = 0, ch1 = 0, out0 = 0;
          for (int a = 0; a < nb_rx_ant; a++) {
            ch0 += abs(chF2[0][a][nre / 2].r) + abs(chF2[0][a][nre / 2].i);
            ch1 += abs(chF2[1][a][nre / 2].r) + abs(chF2[1][a][nre / 2].i);
          }
          // raw received band energy: discriminates "UE absent" (half energy, one UE's worth)
          // from "UE present but despread-cancelled" (full energy, timing-shifted pilots)
          long rxa = 0, rxA[4] = {0, 0, 0, 0}; int rxn = (nre < 256 ? nre : 256);
          // per-antenna PRE-COMBINING SNR: band signal power vs the gNB's own measured noise
          // floor n0_power[a] (idle-RE estimate; = quantization/interference floor in a
          // noiseless sim). Post-combining adds ~10log10(nb_rx_ant) on top of this.
          double presnr[4] = {-99, -99, -99, -99};
          for (int a = 0; a < nb_rx_ant; a++) {
            long t = 0; double p = 0;
            for (int i = 0; i < rxn; i++) {
              t += abs(rxFext[a][i].r) + abs(rxFext[a][i].i);
              p += (double)rxFext[a][i].r * rxFext[a][i].r + (double)rxFext[a][i].i * rxFext[a][i].i;
            }
            if (a < 4) {
              rxA[a] = t / (rxn * 2);
              double n0 = (double)gNB->measurements.n0_power[a];
              if (n0 < 1.0) n0 = 1.0;
              if (p > 0) presnr[a] = 10.0 * log10((p / rxn) / n0);
            }
            rxa += t;
          }
          rxa /= (nb_rx_ant * rxn * 2);
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
          LOG_E(PHY, "[MU METRIC] rnti=%04x port=0x%x rnd=%d partner=%d sym=%d nre=%d Qm=%d oshift=%d mshift=%d rxAbs=%ld rx0=%ld rx1=%ld |chSelf|=%ld |chPart|=%ld corr2pct=%d log2h=%d outAbsMean=%ld llrAbsMean=%ld llrMax=%d postSINR=%.1fdB preSNR=[%.1f %.1f %.1f %.1f]dB cov(irc=%ld nopart=%ld rxe=%ld parte=%ld byp=%ld)\n",
                rel15_ul->rnti, rel15_ul->dmrs_ports, gNB->ulsch[ulsch_id].harq_process->round, partner, symbol, nre,
                rel15_ul->qam_mod_order, output_shift, mu_shift,
                rxa, rxA[0], rxA[1], ch0, ch1, corr2pct, pusch_vars->log2_maxh,
                nre ? out0 / nre : 0, (nre * rel15_ul->qam_mod_order) ? labs / (nre * rel15_ul->qam_mod_order) : 0, lmax,
                sinr_db, presnr[0], presnr[1], presnr[2], presnr[3], mu_c_irc, mu_c_nopart, mu_c_rxe, mu_c_parte, mu_c_byp);
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
  { static int mu_rq = -1; if (mu_rq < 0) { const char *e = getenv("OAI_UL_MU_IRC"); mu_rq = (e && e[0]) ? atoi(e) : 0; }
    extern volatile int g_mu_mimo_active;
    // OAI_UL_CHEST_GUARD=1: apply the unready-estimate requeue in ALL modes, not only MU. The race
    // is antenna-count driven (chest work scales with nb_rx): at 8 RX the DMRS task may not finish
    // before the parallel data-symbol tasks read log2_maxh -> zero channel -> zero LLRs -> the TB
    // dies at any SNR. Gating this on MU meant single-UE 8-RX ate the failures and OLLA was beaten
    // down to MCS 5-9, while co-scheduled UEs (guard active) reached MCS 25 on the same channel.
    static int chest_guard = -1;
    if (chest_guard < 0) { const char *e = getenv("OAI_UL_CHEST_GUARD"); chest_guard = (e && e[0]) ? atoi(e) : 0; }
    if (((mu_rq && g_mu_mimo_active) || chest_guard) && rdata->bounces < 2) { // backstop only: two-phase ordering makes estimates ready before decode
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
        if (rdata->bounces < 200) {
          task_t t = {.func = &nr_pusch_symbol_processing, .args = rdata};
          pushTpool(&gNB->threadPool, t);
          return;
        }
        // Bounce cap: partner estimate never readied. Proceed WITHOUT it (downstream partE<8
        // check skips IRC -> plain decode) so an indication is ALWAYS sent -- an unbounded
        // requeue starves the TB silently and MAC timeout-retransmits (the 8-15% phantom BLER).
        static int requeue_giveups = 0;
        if (requeue_giveups < 50 || (requeue_giveups % 500) == 0)
          printf("[REQUEUE GIVEUP] %d.%d ulsch %d bounces %d total %d\n",
                 rdata->frame, rdata->slot, ulsch_id, rdata->bounces, requeue_giveups);
        requeue_giveups++;
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
    // Capture this TB's mid-slot LLR quality so FAILCLASS can report it for TBs that later fail
    // (the rate-limited MU METRIC misses the failing TBs; this closes that blind spot).
    if (symbol == 6) {
      const int nll = nb_re_pusch * rel15_ul->qam_mod_order;
      long sll = 0;
      for (int i = 0; i < nll; i++) sll += abs(llrss[0][i]);
      pusch_vars->last_llr_mean = nll ? (int32_t)(sll / nll) : -1;
    }
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

  // Slot-scratch from pusch_vars (init-time heap): as stack VLAs these exceed the default
  // 8MB thread stack at 16RX x 273PRB (~8.9MB) and segfault L1_rx_thread in the prologue.
  // Init sized them for max_ul_mimo_layers(4) x N_RB_UL x nb_antennas_rx.
  AssertFatal(nb_layer <= 4 && rel15_ul->rb_size <= frame_parms->N_RB_UL,
              "PUSCH scratch undersized: nb_layer %d rb_size %d N_RB_UL %d\n",
              nb_layer, rel15_ul->rb_size, frame_parms->N_RB_UL);
  c16_t *pusch_dmrs_slot_mem = pusch_vars->dmrs_slot_scratch;
  c16_t *pusch_ch_est_dmrs_pos_slot_mem = pusch_vars->chest_dmrs_pos_scratch;
  c16_t *pusch_ch_est_dmrs_interpl_slot_mem = pusch_vars->chest_interpl_scratch;
  c16_t *rxFext_slot_mem = pusch_vars->rxFext_slot_scratch;

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
    pusch_vars->mu_chest_frame = frame;
    pusch_vars->mu_chest_slot = slot;
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
