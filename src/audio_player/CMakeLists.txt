
set(OPUS_BASE_DIR ${CMAKE_SOURCE_DIR}/external/modules/lib/opus)

file(GLOB_RECURSE OPUS_CELT_SRC
    "${OPUS_BASE_DIR}/celt/bands.c"
    "${OPUS_BASE_DIR}/celt/celt.c"
    "${OPUS_BASE_DIR}/celt/celt_decoder.c"
    "${OPUS_BASE_DIR}/celt/celt_encoder.c"
    "${OPUS_BASE_DIR}/celt/celt_lpc.c"
    "${OPUS_BASE_DIR}/celt/cwrs.c"
    "${OPUS_BASE_DIR}/celt/entcode.c"
    "${OPUS_BASE_DIR}/celt/entdec.c"
    "${OPUS_BASE_DIR}/celt/entenc.c"
    "${OPUS_BASE_DIR}/celt/kiss_fft.c"
    "${OPUS_BASE_DIR}/celt/laplace.c"
    "${OPUS_BASE_DIR}/celt/mathops.c"
    "${OPUS_BASE_DIR}/celt/mdct.c"
    "${OPUS_BASE_DIR}/celt/modes.c"
    "${OPUS_BASE_DIR}/celt/pitch.c"
    "${OPUS_BASE_DIR}/celt/quant_bands.c"  
    "${OPUS_BASE_DIR}/celt/rate.c"
    "${OPUS_BASE_DIR}/celt/vq.c"
    "${OPUS_BASE_DIR}/celt/arm/arm_celt_map.c"
    "${OPUS_BASE_DIR}/celt/arm/armcpu.c"
)

file(GLOB_RECURSE OPUS_SILK_SRC
    "${OPUS_BASE_DIR}/silk/A2NLSF.c"
    "${OPUS_BASE_DIR}/silk/CNG.c"
    "${OPUS_BASE_DIR}/silk/HP_variable_cutoff.c"
    "${OPUS_BASE_DIR}/silk/LPC_analysis_filter.c"
    "${OPUS_BASE_DIR}/silk/LPC_fit.c"
    "${OPUS_BASE_DIR}/silk/LPC_inv_pred_gain.c"
    "${OPUS_BASE_DIR}/silk/LP_variable_cutoff.c"
    "${OPUS_BASE_DIR}/silk/NLSF2A.c"
    "${OPUS_BASE_DIR}/silk/NLSF_VQ.c"
    "${OPUS_BASE_DIR}/silk/NLSF_VQ_weights_laroia.c"
    "${OPUS_BASE_DIR}/silk/NLSF_decode.c"
    "${OPUS_BASE_DIR}/silk/NLSF_del_dec_quant.c"
    "${OPUS_BASE_DIR}/silk/NLSF_encode.c"
    "${OPUS_BASE_DIR}/silk/NLSF_stabilize.c"
    "${OPUS_BASE_DIR}/silk/NLSF_unpack.c"
    "${OPUS_BASE_DIR}/silk/NSQ.c"
    "${OPUS_BASE_DIR}/silk/NSQ_del_dec.c"
    "${OPUS_BASE_DIR}/silk/PLC.c"
    "${OPUS_BASE_DIR}/silk/VAD.c"
    "${OPUS_BASE_DIR}/silk/VQ_WMat_EC.c"
    "${OPUS_BASE_DIR}/silk/ana_filt_bank_1.c"
    "${OPUS_BASE_DIR}/silk/biquad_alt.c"
    "${OPUS_BASE_DIR}/silk/bwexpander.c"
    "${OPUS_BASE_DIR}/silk/bwexpander_32.c"
    "${OPUS_BASE_DIR}/silk/check_control_input.c"
    "${OPUS_BASE_DIR}/silk/code_signs.c"
    "${OPUS_BASE_DIR}/silk/control_SNR.c"
    "${OPUS_BASE_DIR}/silk/control_audio_bandwidth.c"
    "${OPUS_BASE_DIR}/silk/control_codec.c"
    "${OPUS_BASE_DIR}/silk/debug.c"
    "${OPUS_BASE_DIR}/silk/dec_API.c"
    "${OPUS_BASE_DIR}/silk/decode_core.c"
    "${OPUS_BASE_DIR}/silk/decode_frame.c"
    "${OPUS_BASE_DIR}/silk/decode_indices.c"
    "${OPUS_BASE_DIR}/silk/decode_parameters.c"
    "${OPUS_BASE_DIR}/silk/decode_pitch.c"
    "${OPUS_BASE_DIR}/silk/decode_pulses.c"
    "${OPUS_BASE_DIR}/silk/decoder_set_fs.c"
    "${OPUS_BASE_DIR}/silk/enc_API.c"
    "${OPUS_BASE_DIR}/silk/encode_indices.c"
    "${OPUS_BASE_DIR}/silk/encode_pulses.c"
    "${OPUS_BASE_DIR}/silk/gain_quant.c"
    "${OPUS_BASE_DIR}/silk/init_decoder.c"
    "${OPUS_BASE_DIR}/silk/init_encoder.c"
    "${OPUS_BASE_DIR}/silk/inner_prod_aligned.c"
    "${OPUS_BASE_DIR}/silk/interpolate.c"
    "${OPUS_BASE_DIR}/silk/lin2log.c"
    "${OPUS_BASE_DIR}/silk/log2lin.c"
    "${OPUS_BASE_DIR}/silk/pitch_est_tables.c"
    "${OPUS_BASE_DIR}/silk/process_NLSFs.c"
    "${OPUS_BASE_DIR}/silk/quant_LTP_gains.c"
    "${OPUS_BASE_DIR}/silk/resampler.c"
    "${OPUS_BASE_DIR}/silk/resampler_down2.c"
    "${OPUS_BASE_DIR}/silk/resampler_down2_3.c"
    "${OPUS_BASE_DIR}/silk/resampler_private_AR2.c"
    "${OPUS_BASE_DIR}/silk/resampler_private_IIR_FIR.c"
    "${OPUS_BASE_DIR}/silk/resampler_private_down_FIR.c"
    "${OPUS_BASE_DIR}/silk/resampler_private_up2_HQ.c"
    "${OPUS_BASE_DIR}/silk/resampler_rom.c"
    "${OPUS_BASE_DIR}/silk/shell_coder.c"
    "${OPUS_BASE_DIR}/silk/sigm_Q15.c"
    "${OPUS_BASE_DIR}/silk/sort.c"
    "${OPUS_BASE_DIR}/silk/stereo_LR_to_MS.c"
    "${OPUS_BASE_DIR}/silk/stereo_MS_to_LR.c"
    "${OPUS_BASE_DIR}/silk/stereo_decode_pred.c"
    "${OPUS_BASE_DIR}/silk/stereo_encode_pred.c"
    "${OPUS_BASE_DIR}/silk/stereo_find_predictor.c"
    "${OPUS_BASE_DIR}/silk/stereo_quant_pred.c"
    "${OPUS_BASE_DIR}/silk/sum_sqr_shift.c"
    "${OPUS_BASE_DIR}/silk/table_LSF_cos.c"
    "${OPUS_BASE_DIR}/silk/tables_LTP.c"
    "${OPUS_BASE_DIR}/silk/tables_NLSF_CB_NB_MB.c"
    "${OPUS_BASE_DIR}/silk/tables_NLSF_CB_WB.c"
    "${OPUS_BASE_DIR}/silk/tables_gain.c"
    "${OPUS_BASE_DIR}/silk/tables_other.c"
    "${OPUS_BASE_DIR}/silk/tables_pitch_lag.c"
    "${OPUS_BASE_DIR}/silk/tables_pulses_per_block.c"
    "${OPUS_BASE_DIR}/silk/fixed/LTP_analysis_filter_FIX.c"
    "${OPUS_BASE_DIR}/silk/fixed/LTP_scale_ctrl_FIX.c"
    "${OPUS_BASE_DIR}/silk/fixed/apply_sine_window_FIX.c"
    "${OPUS_BASE_DIR}/silk/fixed/autocorr_FIX.c"
    "${OPUS_BASE_DIR}/silk/fixed/burg_modified_FIX.c"
    "${OPUS_BASE_DIR}/silk/fixed/corrMatrix_FIX.c"
    "${OPUS_BASE_DIR}/silk/fixed/encode_frame_FIX.c"
    "${OPUS_BASE_DIR}/silk/fixed/find_LPC_FIX.c"
    "${OPUS_BASE_DIR}/silk/fixed/find_LTP_FIX.c"
    "${OPUS_BASE_DIR}/silk/fixed/find_pitch_lags_FIX.c"
    "${OPUS_BASE_DIR}/silk/fixed/find_pred_coefs_FIX.c"
    "${OPUS_BASE_DIR}/silk/fixed/k2a_FIX.c"
    "${OPUS_BASE_DIR}/silk/fixed/k2a_Q16_FIX.c"
    "${OPUS_BASE_DIR}/silk/fixed/noise_shape_analysis_FIX.c"
    "${OPUS_BASE_DIR}/silk/fixed/pitch_analysis_core_FIX.c"
    "${OPUS_BASE_DIR}/silk/fixed/process_gains_FIX.c"
    "${OPUS_BASE_DIR}/silk/fixed/regularize_correlations_FIX.c"
    "${OPUS_BASE_DIR}/silk/fixed/residual_energy16_FIX.c"
    "${OPUS_BASE_DIR}/silk/fixed/residual_energy_FIX.c"
    "${OPUS_BASE_DIR}/silk/fixed/schur64_FIX.c"
    "${OPUS_BASE_DIR}/silk/fixed/schur_FIX.c"
    "${OPUS_BASE_DIR}/silk/fixed/vector_ops_FIX.c"
    "${OPUS_BASE_DIR}/silk/fixed/warped_autocorrelation_FIX.c"
)

file(GLOB_RECURSE OPUS_SRC
    "${OPUS_BASE_DIR}/src/analysis.c"
    "${OPUS_BASE_DIR}/src/mlp.c"
    "${OPUS_BASE_DIR}/src/mlp_data.c"
    "${OPUS_BASE_DIR}/src/opus.c"
    "${OPUS_BASE_DIR}/src/opus_decoder.c"
    "${OPUS_BASE_DIR}/src/opus_encoder.c"
    "${OPUS_BASE_DIR}/src/opus_multistream.c"
    "${OPUS_BASE_DIR}/src/opus_multistream_decoder.c"
    "${OPUS_BASE_DIR}/src/opus_multistream_encoder.c"
    "${OPUS_BASE_DIR}/src/repacketizer.c"
)

set(OGG_SRC
    ${CMAKE_SOURCE_DIR}/external/modules/lib/ogg/src/framing.c    
    ${CMAKE_SOURCE_DIR}/external/modules/lib/opus-tools/src/opus_header.c
)

set(ATDIO_SRC
    audio_player.c
    opus_interface.c
)

if(DEFINED CONFIG_RPR_MODULE_AUDIO_PLAYER)
target_sources(app PRIVATE 
    ${ATDIO_SRC}
    ${OPUS_CELT_SRC}
    ${OPUS_SILK_SRC}
    ${OPUS_SRC}
    ${OGG_SRC}
)
endif()


target_include_directories(app PRIVATE 
    ${CMAKE_SOURCE_DIR}/src/audio_player/
    ${CMAKE_SOURCE_DIR}/external/modules/lib/opus/src/
    ${CMAKE_SOURCE_DIR}/external/modules/lib/opus/celt/
    ${CMAKE_SOURCE_DIR}/external/modules/lib/opus/include/
    ${CMAKE_SOURCE_DIR}/external/modules/lib/opus/silk/
    ${CMAKE_SOURCE_DIR}/external/modules/lib/opus/silk/arm/
    ${CMAKE_SOURCE_DIR}/external/modules/lib/opus/celt/arm/
    ${CMAKE_SOURCE_DIR}/external/modules/lib/opus/silk/fixed/
    ${CMAKE_SOURCE_DIR}/external/modules/lib/opus/
    ${CMAKE_SOURCE_DIR}/external/modules/lib/ogg/include/
    ${CMAKE_SOURCE_DIR}/external/modules/lib/opus-tools/src/
)

target_compile_definitions(app PRIVATE 
                            VAR_ARRAYS 
                            FIXED_POINT 
                            OPUS_BUILD 
                            OPUS_ARM_ASM 
                            OPUS_ARM_INLINE_ASM 
                            ARM_MATH_CM4 
                            DISABLE_FLOAT_API 
                            DEBUG)

