/* GENERATED FILE - do not edit. Made by tools/gen-charon-hda-quirks.py.
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * HD Audio machine quirks (pin configuration overrides, verb lists and their
 * chains, subsystem-ID lists) taken as data from the Linux kernel:
 *   https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git
 *   tag v7.3, commit c03c1a14507dfaf8b856c2608f9ac216021fc222
 * Copyright of that data: the Linux kernel contributors (see each input file).
 *
 * Input files (SHA-256, licence):
 *   e1cdb6a947df747bac668be534c7ff5d463be351255d8366e4cbe61b924cc29c  GPL-2.0-or-later  include/sound/hda_codec.h
 *   d09163a18bb808ba89cfb9e1f2e30898d9eab7b0cdcf6711acddd9ae67c8e056  GPL-2.0-or-later  sound/hda/codecs/analog.c
 *   ba0a45f07e458b982cf8338b7bc632b4cfbcd3b7ebd2a2f9d40b3161278e9f28  GPL-2.0-or-later  sound/hda/codecs/cirrus/cs420x.c
 *   2cd6ae62b569303322f5ed0846986c52554dc5d90faf2b89da2c841a86ff2d72  GPL-2.0-or-later  sound/hda/codecs/cirrus/cs421x.c
 *   d4dcde1bbdb8b212869cb3972020612e701eed97597249cd0e3c47793b560caa  GPL-2.0-or-later  sound/hda/codecs/conexant.c
 *   9329192af98fe29affdad229366ec38fc1d6610973203988b22c41dfac290ca0  GPL-2.0-or-later  sound/hda/codecs/generic.h
 *   166cbd96f15707762e2cdc38280172e83774d97c348b202635c809d22e5c9bad  GPL-2.0-or-later  sound/hda/codecs/realtek/alc260.c
 *   99925cb6ad0b6051f8ff9713cd3f163dbaa53bdc1f49b0f1f549f3b13e053a34  GPL-2.0-or-later  sound/hda/codecs/realtek/alc262.c
 *   bb1d483f6d17c7b166d5ae9810e2ca09607f0dd27f50e9155eefa2987183c39d  GPL-2.0-or-later  sound/hda/codecs/realtek/alc268.c
 *   becf69c4fa5f2e7aef80f19e23e04abe82123c7f8df9889e906dd76b6d0ac2fc  GPL-2.0-or-later  sound/hda/codecs/realtek/alc269.c
 *   472cd52571d18ddf2bf304b166348282b724311ad4cd93409922042b2a66e93f  GPL-2.0-or-later  sound/hda/codecs/realtek/alc662.c
 *   d344f73fe9f3a516657438ed01b638a5148658d335a19265accc17a51150fb4d  GPL-2.0-or-later  sound/hda/codecs/realtek/alc861.c
 *   68ba77616d38ec8ba9d90d394161ab75606201a973a8f0240d50837999eba590  GPL-2.0-or-later  sound/hda/codecs/realtek/alc861vd.c
 *   2c3551811f46d659f00fc0cca518330723de9e72d9cc16c0765b86c0990a2cee  GPL-2.0-or-later  sound/hda/codecs/realtek/alc880.c
 *   997dae4ee6607372a38734845da79d78230417038f9e6d44c7c8c9c3fde7d89c  GPL-2.0-or-later  sound/hda/codecs/realtek/alc882.c
 *   17016a8f11b3e8f604fee65ac80deb32bbbc7ca6aaf31112ff71b7ab97f19100  GPL-2.0-or-later  sound/hda/codecs/realtek/realtek.h
 *   a643b4d069687189d74a5ffa757a05617819dcb2d50c99ccfff7d5a5900d40c5  GPL-2.0-or-later  sound/hda/codecs/senarytech.c
 *   25eb6a431141ae2b83a8f1aadea5f879d1d4dd190eafc5f5a67ec6ea9a752d03  GPL-2.0-or-later  sound/hda/codecs/sigmatel.c
 *   63c42dca91e471fee45c8cfa825013bb73b1d33b06205c528c4aadb0ef3c88d4  GPL-2.0-or-later  sound/hda/codecs/via.c
 *   48f84d785ab51ce03da16da54efed182b1f2b1fd0cb816ab54d05e2d302fe947  GPL-2.0-or-later  sound/hda/common/hda_auto_parser.h
 *   5333cb766ada5e3fddf4c753947078adf2484dda6c2ec6fe0ee29255a5820633  GPL-2.0-or-later  sound/hda/common/hda_local.h
 *
 * Totals: 1897 subsystem-ID rows, 1128 of them with at least one action this
 * driver performs; 493 fixups kept (252 pin lists, 58 verb lists); 0 unreadable.
 * Function fixups ported by hand: ad1884_fixup_hp_eapd, ad1986a_fixup_eapd, alc1220_fixup_clevo_p950, alc1220_fixup_clevo_pb51ed, alc245_fixup_hp_x360_amp, alc285_fixup_hp_gpio_amp_init, alc287_fixup_yoga9_14iap7_bass_spk_pin, alc885_fixup_macpro_gpio, alc_fixup_gpio1, alc_fixup_gpio2, alc_fixup_gpio3, alc_fixup_gpio4, alc_fixup_sku_ignore, cs4208_fixup_gpio0, cs4208_fixup_mac, cs420x_fixup_gpio_13, cs420x_fixup_gpio_23, stac9200_fixup_panasonic, stac9205_fixup_dell_m43, stac9205_fixup_ref, stac922x_fixup_intel_mac_gpio, stac927x_fixup_dell_dmic, stac927x_fixup_ref, stac92hd71bxx_fixup_ref, stac92hd73xx_fixup_ref, stac92hd83xxx_fixup_gpio10_eapd.
 * Function fixups NOT ported (name: quirk rows whose chain reaches it):
 *   cs35l41_fixup_i2c_two:139 alc_fixup_micmute_led:118 alc_fixup_thinkpad_acpi:109
 *   alc_fixup_headset_mode_no_hp_mic:106 alc_fixup_headset_mode:102 cs35l41_fixup_spi_two:83
 *   alc_fixup_headset_jack:78 stac92hd83xxx_fixup_hp:77 alc285_fixup_hp_gpio_led:76
 *   alc269_fixup_limit_int_mic_boost:64 alc236_fixup_hp_gpio_led:55
 *   alc285_fixup_speaker2_to_dac1:53 alc_fixup_headset_mic:51 stac92hd83xxx_fixup_hp_mic_led:51
 *   alc285_fixup_thinkpad_x1_gen7:49 alc_fixup_disable_aamix:40 tas2781_fixup_tias_i2c:40
 *   alc236_fixup_hp_mute_led_micmute_vref:32 alc_fixup_headset_mode_alc255:32
 *   alc245_fixup_hp_mute_led_coefbit:30 alc269_fixup_hp_mute_led_mic1:30
 *   alc2xx_fixup_headset_mic:30 alc285_fixup_hp_mute_led:23 alc236_fixup_hp_mute_led_coefbit2:22
 *   alc_fixup_tpt440_dock:20 alc287_fixup_tb_vmaster_led:16 alc269_fixup_hp_gpio_mic1_led:15
 *   cxt_fixup_mute_led_gpio:15 alc236_fixup_hp_mute_led_micmute_gpio:14 alc287_fixup_hp_gpio_led:14
 *   alc245_fixup_hp_gpio_led:13 alc_fixup_auto_mute_via_amp:13 alc_fixup_headset_mode_alc668:13
 *   alc_fixup_inv_dmic:13 alc_fixup_tpt470_dacs:13 alc_fixup_tpt470_dock:13
 *   alc298_fixup_samsung_amp:12 alc_fixup_no_shutup:12 stac92hd71bxx_fixup_hp:12
 *   tas2781_fixup_txnw_i2c:12 alc889_fixup_mbp_vref:11 alc_fixup_dell_xps13:11
 *   stac92hd73xx_fixup_dell_m6_dmic:10 alc245_fixup_hp_zbook_firefly_g12a:9
 *   alc269_fixup_hp_mute_led_mic3:9 alc289_fixup_asus_ga401:9 alc897_fixup_lenovo_headset_mic:9
 *   cxt_fixup_stereo_dmic:9 hda_fixup_thinkpad_acpi:9 stac92hd83xxx_fixup_headset_jack:9
 *   alc287_fixup_legion_16iax10h_aw88399:8 alc295_fixup_chromebook:8 alc_fixup_bass_chmap:8
 *   aw88399_fixup_i2c_two:8 cxt_fixup_headset_mic:8 stac92hd71bxx_fixup_hp_dv5:8
 *   alc269_fixup_hp_line1_mic1_led:7 alc269_fixup_pincfg_no_hp_to_lineout:7
 *   alc298_fixup_samsung_amp_v2_4_amps:7 cs35l41_fixup_spi_four:7
 *   alc245_tas2781_i2c_hp_fixup_muteled:6 alc245_tas2781_spi_hp_fixup_muteled:6
 *   alc269_fixup_quanta_mute:6 alc285_fixup_invalidate_dacs:6 alc290_fixup_mono_speakers:6
 *   alc880_fixup_vol_knob:6 alc889_fixup_imac91_vref:6 alc233_fixup_lenovo_gpio2_mic_hotkey:5
 *   alc269_fixup_stereo_dmic:5 alc287_fixup_legion_15imhg05_speakers:5 cxt_fixup_cap_mix_amp:5
 *   ad1981_fixup_amp_override:4 alc245_fixup_hp_spectre_x360_eu0xxx:4 alc271_hp_gate_mic_jack:4
 *   alc285_fixup_hp_coef_micmute_led:4 alc285_fixup_hp_spectre_x360_eb1:4
 *   alc298_fixup_samsung_amp_v2_2_amps:4 alc671_fixup_hp_headset_mic2:4
 *   alc882_fixup_no_primary_hp:4 alc1220_fixup_gb_dual_codecs:3 alc1220_fixup_gb_x570:3
 *   alc233_fixup_lenovo_low_en_micmute_led:3 alc245_fixup_hp_mute_led_v1_coefbit:3
 *   alc245_fixup_hp_mute_led_v2_coefbit:3 alc256_fixup_acer_sfg16_micmute_led:3
 *   alc274_fixup_bind_dacs:3 alc275_fixup_gpio4_off:3 alc285_fixup_asus_ga403u:3
 *   alc285_fixup_ideapad_s740_coef:3 alc295_fixup_dell_inspiron_top_speakers:3
 *   alc295_fixup_hp_top_speakers:3 alc298_fixup_speaker_volume:3 alc662_fixup_led_gpio1:3
 *   alc662_fixup_usi_headset_mic:3 alc861_fixup_asus_amp_vref_0f:3 alc889_fixup_mba11_vref:3
 *   alc_fixup_ideapad_acpi:3 alc_fixup_no_jack_detect:3 alc_fixup_yoga_pro7_audio:3
 *   ad1884_fixup_amp_override:2 ad1981_fixup_hp_eapd:2 alc233_alc662_fixup_lenovo_dual_codecs:2
 *   alc233_fixup_lenovo_line2_mic_hotkey:2 alc233_fixup_no_audio_jack:2
 *   alc233_fixup_starlabs_starfighter:2 alc236_fixup_hp_mute_led:2
 *   alc245_fixup_hp_spectre_x360_16_aa0xxx:2 alc245_tas2781_i2c_hp_fixup_muteled_inverted:2
 *   alc256_decrease_headphone_amp_val:2 alc260_fixup_gpio1_toggle:2 alc269_fixup_hweq:2
 *   alc274_fixup_hp_aio_bind_dacs:2 alc274_fixup_hp_headset_mic:2 alc280_fixup_hp_gpio4:2
 *   alc287_fixup_legion_16ithg6_speakers:2 alc287_fixup_lenovo_thinkpad_with_alc1318:2
 *   alc294_fixup_bass_speaker_15:2 alc298_fixup_razer_blade16_2025:2 alc861vd_fixup_dallas:2
 *   alc_fixup_disable_mic_vref:2 alc_fixup_headset_mode_dell_alc288:2 alc_fixup_no_int_mic:2
 *   cs35l41_fixup_i2c_four:2 cxt_fixup_hp_zbook_mute_led:2 stac9205_fixup_eapd:2
 *   stac92hd71bxx_fixup_hp_hdx:2 stac92hd73xx_fixup_alienware_m17x:2
 *   stac92hd73xx_fixup_dell_m6_amic:2 stac92hd73xx_fixup_dell_m6_both:2
 *   stac92hd83xxx_fixup_hp_led:2 tas2781_fixup_spi:2 via_fixup_power_save:2
 *   yoga7_14arb7_fixup_i2c:2 ad1884_fixup_thinkpad:1 ad1986a_fixup_eapd_mix_in:1
 *   ad_fixup_inv_jack_detect:1 alc225_fixup_s3_pop_noise:1 alc236_fixup_hp_15_fd0xxx:1
 *   alc245_fixup_hp_envy_x360_mute_led:1 alc245_fixup_minisforum_jack_detect:1
 *   alc256_fixup_mic_no_presence_and_resume:1 alc256_fixup_set_coef_defaults:1
 *   alc256_fixup_xiaomi_pro15_resume:1 alc260_fixup_fsc_s7020:1 alc260_fixup_kn1:1
 *   alc269_fixup_hp_gpio_led:1 alc269_fixup_hp_mute_led_mic2:1 alc269_fixup_pcm_44k:1
 *   alc269_fixup_pincfg_U7x7_headset_mic:1 alc269_fixup_x101_headset_mic:1
 *   alc269vb_fixup_aspire_e1_coef:1 alc271_fixup_dmic:1 alc274_fixup_hp_89e9_amp:1
 *   alc274_fixup_hp_envy_gpio:1 alc280_fixup_hp_9480m:1 alc280_fixup_hp_gpio2_mic_hotkey:1
 *   alc282_fixup_asus_tx300:1 alc283_fixup_chromebook:1 alc283_fixup_dell_hp_resume:1
 *   alc285_fixup_hp_beep:1 alc285_fixup_hp_envy_x360:1 alc285_fixup_hp_spectre_x360:1
 *   alc285_fixup_hp_spectre_x360_df1:1 alc285_fixup_hp_spectre_x360_mute_led:1
 *   alc285_lenovo_dac_rename:1 alc286_fixup_hp_gpio_led:1 alc287_fixup_acer_micmute_led:1
 *   alc287_fixup_legion_16achg6_speakers:1 alc287_fixup_lenovo_yoga_book_9i:1
 *   alc288_fixup_surface_swap_dacs:1 alc294_fixup_gu502_hp:1 alc294_fixup_gx502_hp:1
 *   alc295_fixup_asus_dacs:1 alc295_fixup_disable_dac3:1 alc295_fixup_hp_mute_led_coefbit11:1
 *   alc295_fixup_hp_pavilion_mute_led_1b:1 alc298_fixup_lenovo_c940_duet7:1
 *   alc298_fixup_lg_gram_style_14:1 alc660vd_fixup_asus_gpio1:1 alc662_fixup_aspire_ethos_hp:1
 *   alc662_fixup_csl_amp:1 alc700_fixup_intel_hades_canyon:1 alc887_fixup_asus_jack:1
 *   alc889_fixup_coef:1 alc889_fixup_dac_route:1 alc889_fixup_mba21_vref:1
 *   alc897_fixup_lenovo_headset_mode:1 alc898_fixup_clevo_p775tm1:1
 *   alc_fixup_dell4_mic_no_presence_quiet:1 alc_fixup_headset_mode_acer_coldboot:1
 *   alc_fixup_headset_mode_alc255_no_hp_mic:1 alc_fixup_no_depop_delay:1 cs35l41_fixup_spi_one:1
 *   cs421x_fixup_sense_b:1 cxt5066_increase_mic_boost:1 cxt_fixup_cap_mix_amp_5047:1
 *   cxt_fixup_headphone_mic:1 cxt_fixup_hp_a_u:1 cxt_fixup_hp_gate_mic_jack:1
 *   cxt_fixup_mute_led_eapd:1 cxt_fixup_olpc_xo:1 cxt_fixup_update_pinctl:1
 *   hda_fixup_ideapad_acpi:1 hp_envy_ts_fixup_dac_bind:1 stac922x_fixup_intel_mac_auto:1
 *   stac927x_fixup_volknob:1 stac92hd71bxx_fixup_hp_dv4:1 stac92hd71bxx_fixup_hp_m4:1
 *   stac92hd73xx_disable_automute:1 stac92hd73xx_fixup_dell_eq:1 stac92hd83xxx_fixup_hp_inv_led:1
 *   stac92hd83xxx_fixup_hp_led_gpio10:1 stac92hd83xxx_fixup_hp_zephyr:1 stac92hd95_fixup_hp_led:1
 *   via_fixup_intmic_boost:1
 */
#ifndef HAK_HDA_QUIRK_TABLE_H
#define HAK_HDA_QUIRK_TABLE_H

#define HAK_HDA_QUIRK_KERNEL_TAG     "v7.3"
#define HAK_HDA_QUIRK_KERNEL_COMMIT  "c03c1a14507dfaf8b856c2608f9ac216021fc222"
#define HAK_HDA_QUIRK_ROWS           1897
#define HAK_HDA_QUIRK_ROWS_ACTIVE    1128

/* alc269: sound/hda/codecs/realtek/alc269.c  (alc269_fixups + alc269_fixup_tbl) */
static CONST HAK_HDA_CODEC_ID mHakIds_alc269[] = {
  { 0x10ec0215, 0x000000 },  /* ALC215 */
  { 0x10ec0221, 0x000000 },  /* ALC221 */
  { 0x10ec0222, 0x000000 },  /* ALC222 */
  { 0x10ec0225, 0x000000 },  /* ALC225 */
  { 0x10ec0230, 0x000000 },  /* ALC236 */
  { 0x10ec0231, 0x000000 },  /* ALC231 */
  { 0x10ec0233, 0x000000 },  /* ALC233 */
  { 0x10ec0234, 0x000000 },  /* ALC234 */
  { 0x10ec0235, 0x000000 },  /* ALC233 */
  { 0x10ec0236, 0x000000 },  /* ALC236 */
  { 0x10ec0245, 0x000000 },  /* ALC245 */
  { 0x10ec0255, 0x000000 },  /* ALC255 */
  { 0x10ec0256, 0x000000 },  /* ALC256 */
  { 0x10ec0257, 0x000000 },  /* ALC257 */
  { 0x10ec0269, 0x000000 },  /* ALC269 */
  { 0x10ec0270, 0x000000 },  /* ALC270 */
  { 0x10ec0274, 0x000000 },  /* ALC274 */
  { 0x10ec0275, 0x000000 },  /* ALC275 */
  { 0x10ec0276, 0x000000 },  /* ALC276 */
  { 0x10ec0280, 0x000000 },  /* ALC280 */
  { 0x10ec0282, 0x000000 },  /* ALC282 */
  { 0x10ec0283, 0x000000 },  /* ALC283 */
  { 0x10ec0284, 0x000000 },  /* ALC284 */
  { 0x10ec0285, 0x000000 },  /* ALC285 */
  { 0x10ec0286, 0x000000 },  /* ALC286 */
  { 0x10ec0287, 0x000000 },  /* ALC287 */
  { 0x10ec0288, 0x000000 },  /* ALC288 */
  { 0x10ec0289, 0x000000 },  /* ALC289 */
  { 0x10ec0290, 0x000000 },  /* ALC290 */
  { 0x10ec0292, 0x000000 },  /* ALC292 */
  { 0x10ec0293, 0x000000 },  /* ALC293 */
  { 0x10ec0294, 0x000000 },  /* ALC294 */
  { 0x10ec0295, 0x000000 },  /* ALC295 */
  { 0x10ec0298, 0x000000 },  /* ALC298 */
  { 0x10ec0299, 0x000000 },  /* ALC299 */
  { 0x10ec0300, 0x000000 },  /* ALC300 */
  { 0x10ec0623, 0x000000 },  /* ALC623 */
  { 0x10ec0700, 0x000000 },  /* ALC700 */
  { 0x10ec0701, 0x000000 },  /* ALC701 */
  { 0x10ec0703, 0x000000 },  /* ALC703 */
  { 0x10ec0711, 0x000000 },  /* ALC711 */
  { 0x19e58326, 0x000000 },  /* HW8326 */
};
static CONST HAK_HDA_PINCFG mHakPins_alc269[] = {
  { 0x12, 0x99a3092f },
  { 0x14, 0x99130110 },
  { 0x18, 0x03a11c20 },
  { 0x1e, 0x0346101e },
  { 0x21, 0x0321101f },
  { 0x00, 0x00000000 },
  { 0x19, 0x01a1913c },
  { 0x00, 0x00000000 },
  { 0x14, 0x99130110 },
  { 0x19, 0x01a19c20 },
  { 0x1b, 0x99a7012f },
  { 0x21, 0x0121401f },
  { 0x00, 0x00000000 },
  { 0x12, 0x90a60130 },
  { 0x14, 0x90170110 },
  { 0x17, 0x40000008 },
  { 0x18, 0x411111f0 },
  { 0x19, 0x01a1913c },
  { 0x1a, 0x411111f0 },
  { 0x1b, 0x411111f0 },
  { 0x1d, 0x40f89b2d },
  { 0x1e, 0x411111f0 },
  { 0x21, 0x0321101f },
  { 0x00, 0x00000000 },
  { 0x19, 0x01a1913c },
  { 0x00, 0x00000000 },
  { 0x1a, 0x2101103f },
  { 0x1b, 0x23a11040 },
  { 0x00, 0x00000000 },
  { 0x18, 0x01a1913c },
  { 0x00, 0x00000000 },
  { 0x18, 0x02a11030 },
  { 0x00, 0x00000000 },
  { 0x17, 0x90170151 },
  { 0x1b, 0x90170152 },
  { 0x00, 0x00000000 },
  { 0x21, 0x90170150 },
  { 0x00, 0x00000000 },
  { 0x14, 0x90100120 },
  { 0x18, 0x02a111f0 },
  { 0x1a, 0x01011020 },
  { 0x00, 0x00000000 },
  { 0x18, 0x01a11130 },
  { 0x00, 0x00000000 },
  { 0x19, 0x02a1113c },
  { 0x1a, 0x90a1092f },
  { 0x00, 0x00000000 },
  { 0x19, 0x02a11120 },
  { 0x00, 0x00000000 },
  { 0x19, 0x01a1913c },
  { 0x00, 0x00000000 },
  { 0x19, 0x03a11130 },
  { 0x1a, 0x90a60140 },
  { 0x00, 0x00000000 },
  { 0x21, 0x03211030 },
  { 0x00, 0x00000000 },
  { 0x19, 0x03a11020 },
  { 0x00, 0x00000000 },
  { 0x16, 0x21014020 },
  { 0x19, 0x21a19030 },
  { 0x1a, 0x01a1913c },
  { 0x00, 0x00000000 },
  { 0x17, 0x90170112 },
  { 0x00, 0x00000000 },
  { 0x19, 0x01a1913c },
  { 0x1a, 0x01a1913d },
  { 0x00, 0x00000000 },
  { 0x17, 0x90170112 },
  { 0x00, 0x00000000 },
  { 0x1a, 0x01a1913c },
  { 0x00, 0x00000000 },
  { 0x18, 0x01a1913d },
  { 0x1a, 0x01a1913c },
  { 0x00, 0x00000000 },
  { 0x18, 0x01a1913c },
  { 0x1a, 0x01a1913d },
  { 0x00, 0x00000000 },
  { 0x19, 0x01a1913c },
  { 0x1a, 0x01a1913d },
  { 0x00, 0x00000000 },
  { 0x1b, 0x90170151 },
  { 0x00, 0x00000000 },
  { 0x18, 0x01a1913c },
  { 0x00, 0x00000000 },
  { 0x16, 0x0101102f },
  { 0x19, 0x02a1913c },
  { 0x1a, 0x01a19030 },
  { 0x1b, 0x02011020 },
  { 0x00, 0x00000000 },
  { 0x1b, 0x0401102f },
  { 0x00, 0x00000000 },
  { 0x19, 0x01a1913c },
  { 0x00, 0x00000000 },
  { 0x14, 0x0201101f },
  { 0x00, 0x00000000 },
  { 0x19, 0x01a1913c },
  { 0x1b, 0x01a1913d },
  { 0x00, 0x00000000 },
  { 0x16, 0x01011020 },
  { 0x19, 0x01a1913c },
  { 0x00, 0x00000000 },
  { 0x17, 0x90170130 },
  { 0x00, 0x00000000 },
  { 0x14, 0x90170140 },
  { 0x00, 0x00000000 },
  { 0x1b, 0x21011020 },
  { 0x18, 0x2181103f },
  { 0x00, 0x00000000 },
  { 0x1b, 0x21011020 },
  { 0x1a, 0x01a1903c },
  { 0x18, 0x2181103f },
  { 0x00, 0x00000000 },
  { 0x19, 0x01a1913c },
  { 0x1a, 0x01813030 },
  { 0x00, 0x00000000 },
  { 0x18, 0x01a1913c },
  { 0x1a, 0x01a1913d },
  { 0x00, 0x00000000 },
  { 0x19, 0x02a19020 },
  { 0x00, 0x00000000 },
  { 0x12, 0xb7a60130 },
  { 0x13, 0x40000000 },
  { 0x14, 0x411111f0 },
  { 0x16, 0x411111f0 },
  { 0x17, 0x90170110 },
  { 0x18, 0x411111f0 },
  { 0x19, 0x02a11030 },
  { 0x1a, 0x411111f0 },
  { 0x1b, 0x04a19030 },
  { 0x1d, 0x40600001 },
  { 0x1e, 0x411111f0 },
  { 0x21, 0x03211020 },
  { 0x00, 0x00000000 },
  { 0x14, 0x90170150 },
  { 0x00, 0x00000000 },
  { 0x12, 0x90a60160 },
  { 0x00, 0x00000000 },
  { 0x19, 0x03a11050 },
  { 0x1b, 0x03a11c30 },
  { 0x00, 0x00000000 },
  { 0x19, 0x04a11120 },
  { 0x00, 0x00000000 },
  { 0x19, 0x03a11050 },
  { 0x1b, 0x03a11c30 },
  { 0x00, 0x00000000 },
  { 0x13, 0x90a60160 },
  { 0x19, 0x04a11120 },
  { 0x00, 0x00000000 },
  { 0x19, 0x01a1103c },
  { 0x00, 0x00000000 },
  { 0x19, 0x01a1913c },
  { 0x00, 0x00000000 },
  { 0x19, 0x03a11050 },
  { 0x1a, 0x03a11c30 },
  { 0x21, 0x03211420 },
  { 0x00, 0x00000000 },
  { 0x18, 0x01a110f0 },
  { 0x00, 0x00000000 },
  { 0x19, 0x03a11050 },
  { 0x1b, 0x03a11c30 },
  { 0x00, 0x00000000 },
  { 0x12, 0x99a3092f },
  { 0x14, 0x99130110 },
  { 0x18, 0x01a19c20 },
  { 0x21, 0x0121401f },
  { 0x00, 0x00000000 },
  { 0x19, 0x03a11050 },
  { 0x1b, 0x03a11c30 },
  { 0x00, 0x00000000 },
  { 0x19, 0x03a11050 },
  { 0x1b, 0x03a11c30 },
  { 0x00, 0x00000000 },
  { 0x14, 0x90170152 },
  { 0x19, 0x03a19020 },
  { 0x1a, 0x03a11c30 },
  { 0x1e, 0x90170151 },
  { 0x21, 0x03211420 },
  { 0x00, 0x00000000 },
  { 0x17, 0x90170151 },
  { 0x1e, 0x90170150 },
  { 0x00, 0x00000000 },
  { 0x19, 0x03a11050 },
  { 0x1a, 0x03a11c30 },
  { 0x21, 0x03211420 },
  { 0x00, 0x00000000 },
  { 0x19, 0x03a11050 },
  { 0x1a, 0x01a11830 },
  { 0x21, 0x03211020 },
  { 0x00, 0x00000000 },
  { 0x19, 0x03a11020 },
  { 0x00, 0x00000000 },
  { 0x19, 0x01a1913c },
  { 0x00, 0x00000000 },
  { 0x19, 0x03a19020 },
  { 0x00, 0x00000000 },
  { 0x17, 0x99130111 },
  { 0x00, 0x00000000 },
  { 0x19, 0x04a11020 },
  { 0x00, 0x00000000 },
  { 0x1a, 0x04a19040 },
  { 0x00, 0x00000000 },
  { 0x14, 0x90170120 },
  { 0x00, 0x00000000 },
  { 0x19, 0x01a11050 },
  { 0x1a, 0x01a11830 },
  { 0x21, 0x012110f0 },
  { 0x00, 0x00000000 },
  { 0x19, 0x03a11050 },
  { 0x1a, 0x03a11c30 },
  { 0x21, 0x03211420 },
  { 0x00, 0x00000000 },
  { 0x19, 0x03a11050 },
  { 0x00, 0x00000000 },
  { 0x18, 0x04a1182c },
  { 0x00, 0x00000000 },
  { 0x19, 0x00000022 },
  { 0x00, 0x00000000 },
  { 0x18, 0x01a1913c },
  { 0x00, 0x00000000 },
  { 0x21, 0x0221102f },
  { 0x00, 0x00000000 },
  { 0x19, 0x01a1903c },
  { 0x00, 0x00000000 },
  { 0x19, 0x04a11040 },
  { 0x21, 0x04211020 },
  { 0x00, 0x00000000 },
  { 0x19, 0x04a110f0 },
  { 0x00, 0x00000000 },
  { 0x19, 0x03a11120 },
  { 0x00, 0x00000000 },
  { 0x1a, 0x01a1913c },
  { 0x00, 0x00000000 },
  { 0x12, 0x99a3092f },
  { 0x14, 0x99130110 },
  { 0x15, 0x0121401f },
  { 0x18, 0x01a19c20 },
  { 0x00, 0x00000000 },
  { 0x19, 0x03a1113c },
  { 0x1b, 0x20a11040 },
  { 0x00, 0x00000000 },
  { 0x19, 0x03a1113c },
  { 0x1a, 0x22a190a0 },
  { 0x00, 0x00000000 },
  { 0x19, 0x23a11040 },
  { 0x1b, 0x2121103f },
  { 0x00, 0x00000000 },
  { 0x12, 0x90a60130 },
  { 0x13, 0x40000000 },
  { 0x14, 0x90170110 },
  { 0x18, 0x411111f0 },
  { 0x19, 0x04a11040 },
  { 0x1a, 0x411111f0 },
  { 0x1b, 0x90170112 },
  { 0x1d, 0x40759a05 },
  { 0x1e, 0x411111f0 },
  { 0x21, 0x04211020 },
  { 0x00, 0x00000000 },
  { 0x14, 0x90170121 },
  { 0x17, 0x90170120 },
  { 0x00, 0x00000000 },
  { 0x12, 0x411111f0 },
  { 0x14, 0x90170110 },
  { 0x15, 0x032f1020 },
  { 0x17, 0x411111f0 },
  { 0x18, 0x03ab1040 },
  { 0x19, 0xb7a7013f },
  { 0x1a, 0x0181305f },
  { 0x1b, 0x411111f0 },
  { 0x1d, 0x411111f0 },
  { 0x1e, 0x411111f0 },
  { 0x00, 0x00000000 },
  { 0x12, 0x4000c000 },
  { 0x14, 0x90170110 },
  { 0x15, 0x0421401f },
  { 0x17, 0x411111f0 },
  { 0x18, 0x04a19020 },
  { 0x19, 0x411111f0 },
  { 0x1a, 0x411111f0 },
  { 0x1b, 0x411111f0 },
  { 0x1d, 0x40448505 },
  { 0x1e, 0x411111f0 },
  { 0x20, 0x8000ffff },
  { 0x00, 0x00000000 },
  { 0x12, 0x40000000 },
  { 0x14, 0x01014010 },
  { 0x15, 0x411111f0 },
  { 0x16, 0x411111f0 },
  { 0x18, 0x01a19020 },
  { 0x19, 0x02a19021 },
  { 0x1a, 0x0181302f },
  { 0x1b, 0x0221401f },
  { 0x1c, 0x411111f0 },
  { 0x1d, 0x4044c601 },
  { 0x1e, 0x411111f0 },
  { 0x00, 0x00000000 },
  { 0x12, 0x99a3092f },
  { 0x18, 0x03a11d20 },
  { 0x19, 0x411111f0 },
  { 0x00, 0x00000000 },
  { 0x12, 0x40000000 },
  { 0x14, 0x90170110 },
  { 0x17, 0x411111f0 },
  { 0x18, 0x03a19040 },
  { 0x19, 0x90a70130 },
  { 0x1a, 0x411111f0 },
  { 0x1b, 0x411111f0 },
  { 0x1d, 0x40489d2d },
  { 0x1e, 0x411111f0 },
  { 0x20, 0x0003ffff },
  { 0x21, 0x03214020 },
  { 0x00, 0x00000000 },
  { 0x14, 0x99130110 },
  { 0x15, 0x0121401f },
  { 0x18, 0x01a19c20 },
  { 0x19, 0x99a3092f },
  { 0x1b, 0x0201401f },
  { 0x00, 0x00000000 },
  { 0x18, 0x01a1913c },
  { 0x1a, 0x01a1913d },
  { 0x00, 0x00000000 },
  { 0x1b, 0x90170110 },
  { 0x00, 0x00000000 },
  { 0x1b, 0x90170150 },
  { 0x00, 0x00000000 },
  { 0x14, 0x90170111 },
  { 0x19, 0x03a1113c },
  { 0x1a, 0x22a190a0 },
  { 0x1b, 0x90170110 },
  { 0x00, 0x00000000 },
  { 0x1b, 0x90170110 },
  { 0x00, 0x00000000 },
  { 0x14, 0x90170151 },
  { 0x1b, 0x90170152 },
  { 0x00, 0x00000000 },
  { 0x18, 0x03a19020 },
  { 0x1b, 0x90170150 },
  { 0x00, 0x00000000 },
  { 0x14, 0x411111f0 },
  { 0x1b, 0x90170110 },
  { 0x00, 0x00000000 },
  { 0x1b, 0x90170150 },
  { 0x00, 0x00000000 },
  { 0x1b, 0x90170150 },
  { 0x00, 0x00000000 },
  { 0x1b, 0x01a1913c },
  { 0x00, 0x00000000 },
  { 0x19, 0x01a1913c },
  { 0x00, 0x00000000 },
  { 0x19, 0x02a1112c },
  { 0x00, 0x00000000 },
};
static CONST UINT32 mHakVerbs_alc269[] = {
  0x2005004f,
  0x20045029,
  0x00000000,
  0x20050045,
  0x20045089,
  0x00000000,
  0x20050045,
  0x20045089,
  0x00000000,
  0x2005000d,
  0x20044040,
  0x00000000,
  0x2005001f,
  0x200400c0,
  0x20050030,
  0x200400b1,
  0x00000000,
  0x20050036,
  0x200457d7,
  0x00000000,
  0x20050007,
  0x20047770,
  0x2005000d,
  0x20043000,
  0x00000000,
  0x20050010,
  0x20040220,
  0x00000000,
  0x20050019,
  0x20048e11,
  0x00000000,
  0x2005000b,
  0x20047778,
  0x20050010,
  0x2004c580,
  0x20050026,
  0x20045757,
  0x20050062,
  0x2004a007,
  0x2005006b,
  0x20040060,
  0x00000000,
  0x20050040,
  0x20048800,
  0x2005000f,
  0x20047774,
  0x00000000,
  0x20050040,
  0x20048800,
  0x00000000,
  0x20050062,
  0x2004a007,
  0x20050010,
  0x20048420,
  0x2005000f,
  0x20047774,
  0x00000000,
  0x20050012,
  0x20042800,
  0x00000000,
  0x20050010,
  0x20047f20,
  0x00000000,
  0x20050045,
  0x20045089,
  0x20050046,
  0x20040004,
  0x20050047,
  0x2004a47a,
  0x20050049,
  0x20040049,
  0x2005004a,
  0x2004201b,
  0x2005006b,
  0x20044278,
  0x00000000,
  0x150707c0,
  0x1503b000,
  0x00000000,
  0x2005000f,
  0x20047774,
  0x00000000,
  0x2005000f,
  0x20047778,
  0x00000000,
  0x2005001b,
  0x20044e4b,
  0x00000000,
  0x150707c0,
  0x1503b000,
  0x1b070724,
  0x00000000,
  0x20050010,
  0x2004c420,
  0x20050040,
  0x20048800,
  0x20050049,
  0x20040249,
  0x2005004a,
  0x2004202b,
  0x20050062,
  0x2004a007,
  0x2005006b,
  0x20045060,
  0x00000000,
  0x18070700,
  0x20050008,
  0x20040310,
  0x00000000,
  0x20050045,
  0x20045289,
  0x2005004a,
  0x2004001b,
  0x58050000,
  0x58043888,
  0x2005006f,
  0x20042c0b,
  0x00000000,
  0x1a0707c5,
  0x00000000,
  0x20050008,
  0x20042fcf,
  0x00000000,
  0x20050024,
  0x20040041,
  0x20050026,
  0x2004000c,
  0x20040000,
  0x2004001a,
  0x2004b020,
  0x20050026,
  0x2004000f,
  0x20040000,
  0x20040042,
  0x2004b020,
  0x20050026,
  0x20040010,
  0x20040000,
  0x20040040,
  0x2004b020,
  0x20050026,
  0x20040002,
  0x20040000,
  0x20040000,
  0x2004b020,
  0x20050024,
  0x20040046,
  0x20050026,
  0x2004000c,
  0x20040000,
  0x2004002a,
  0x2004b020,
  0x20050026,
  0x2004000f,
  0x20040000,
  0x20040046,
  0x2004b020,
  0x20050026,
  0x20040010,
  0x20040000,
  0x20040044,
  0x2004b020,
  0x20050026,
  0x20040002,
  0x20040000,
  0x20040000,
  0x2004b020,
  0x00000000,
  0x20050024,
  0x20040041,
  0x20050026,
  0x2004000c,
  0x20040000,
  0x2004001a,
  0x2004b020,
  0x20050026,
  0x20040002,
  0x20040000,
  0x20040000,
  0x2004b020,
  0x20050024,
  0x20040046,
  0x20050026,
  0x2004000c,
  0x20040000,
  0x2004002a,
  0x2004b020,
  0x20050026,
  0x20040002,
  0x20040000,
  0x20040000,
  0x2004b020,
  0x00000000,
  0x20050024,
  0x20040041,
  0x20050026,
  0x2004000c,
  0x20040000,
  0x2004001a,
  0x2004b020,
  0x20050026,
  0x20040002,
  0x20040000,
  0x20040000,
  0x2004b020,
  0x20050024,
  0x20040042,
  0x20050026,
  0x2004000c,
  0x20040000,
  0x2004002a,
  0x2004b020,
  0x20050026,
  0x20040002,
  0x20040000,
  0x20040000,
  0x2004b020,
  0x00000000,
  0x20050024,
  0x20040041,
  0x20050026,
  0x20040002,
  0x20040000,
  0x20040000,
  0x2004b020,
  0x20050024,
  0x20040042,
  0x20050026,
  0x20040002,
  0x20040000,
  0x20040000,
  0x2004b020,
  0x00000000,
  0x2005001a,
  0x20040011,
  0x00000000,
  0x14070c00,
  0x00000000,
};
static CONST HAK_HDA_FIXUP mHakFixups_alc269[] = {
  { 1, 1, 1, 0, 0x0 },  /* 0 ALC269_FIXUP_ACER_AC700 */
  { 0, 0, 0, 0, 0x0 },  /* 1 ALC271_FIXUP_DMIC alc271_fixup_dmic() NOT PORTED */
  { 1, 1, 3, 6, 0x0 },  /* 2 ALC269_FIXUP_ASPIRE_HEADSET_MIC */
  { 0, 1, 4, 0, 0x0 },  /* 3 ALC269_FIXUP_HEADSET_MODE alc_fixup_headset_mode() NOT PORTED */
  { 0, 0, 0, 0, 0x0 },  /* 4 ALC255_FIXUP_MIC_MUTE_LED alc_fixup_micmute_led() NOT PORTED */
  { 0, 1, 6, 0, 0x0 },  /* 5 ALC271_FIXUP_HP_GATE_MIC_JACK alc271_hp_gate_mic_jack() NOT PORTED */
  { 1, 0, 0, 8, 0x0 },  /* 6 ALC271_FIXUP_AMIC_MIC2 */
  { 0, 1, 5, 0, 0x0 },  /* 7 ALC271_FIXUP_HP_GATE_MIC_JACK_E1_572 alc269_fixup_limit_int_mic_boost() NOT PORTED */
  { 1, 0, 0, 13, 0x0 },  /* 8 ALC282_FIXUP_ASPIRE_V5_PINS */
  { 0, 1, 10, 0, 0x0 },  /* 9 ALC255_FIXUP_ACER_LIMIT_INT_MIC_BOOST alc269_fixup_limit_int_mic_boost() NOT PORTED */
  { 1, 1, 11, 24, 0x0 },  /* 10 ALC255_FIXUP_ACER_MIC_NO_PRESENCE */
  { 0, 1, 4, 0, 0x0 },  /* 11 ALC255_FIXUP_HEADSET_MODE alc_fixup_headset_mode_alc255() NOT PORTED */
  { 1, 1, 13, 26, 0x0 },  /* 12 ALC269_FIXUP_LIFEBOOK */
  { 0, 0, 0, 0, 0x0 },  /* 13 ALC269_FIXUP_QUANTA_MUTE alc269_fixup_quanta_mute() NOT PORTED */
  { 1, 1, 15, 29, 0x0 },  /* 14 ALC286_FIXUP_ACER_AIO_MIC_NO_PRESENCE */
  { 0, 0, 0, 0, 0x0 },  /* 15 ALC269_FIXUP_HEADSET_MIC alc_fixup_headset_mic() NOT PORTED */
  { 1, 1, 15, 31, 0x0 },  /* 16 ALC269VC_FIXUP_ACER_HEADSET_MIC */
  { 1, 0, 0, 33, 0x0 },  /* 17 ALC255_FIXUP_PREDATOR_SUBWOOFER */
  { 1, 0, 0, 36, 0x0 },  /* 18 ALC299_FIXUP_PREDATOR_SPK */
  { 1, 1, 15, 38, 0x0 },  /* 19 ALC269VC_FIXUP_ACER_VCOPPERBOX_PINS */
  { 1, 1, 15, 42, 0x0 },  /* 20 ALC269VC_FIXUP_ACER_MIC_NO_PRESENCE */
  { 1, 1, 22, 44, 0x0 },  /* 21 ALC256_FIXUP_ACER_HEADSET_MIC */
  { 0, 0, 0, 0, 0x0 },  /* 22 ALC269_FIXUP_HEADSET_MODE_NO_HP_MIC alc_fixup_headset_mode_no_hp_mic() NOT PORTED */
  { 1, 1, 24, 47, 0x0 },  /* 23 ALC256_FIXUP_ACER_MIC_NO_PRESENCE */
  { 0, 0, 0, 0, 0x0 },  /* 24 ALC256_FIXUP_ASUS_HEADSET_MODE alc_fixup_headset_mode() NOT PORTED */
  { 2, 1, 14, 0, 0x0 },  /* 25 ALC286_FIXUP_ACER_AIO_HEADSET_MIC */
  { 2, 1, 27, 3, 0x0 },  /* 26 ALC233_FIXUP_ACER_HEADSET_MIC */
  { 1, 1, 15, 49, 0x0 },  /* 27 ALC233_FIXUP_ASUS_MIC_NO_PRESENCE */
  { 1, 1, 29, 51, 0x0 },  /* 28 ALC255_FIXUP_ACER_HEADSET_MIC */
  { 0, 0, 0, 0, 0x0 },  /* 29 ALC255_FIXUP_HEADSET_MODE_NO_HP_MIC alc_fixup_headset_mode_alc255_no_hp_mic() NOT PORTED */
  { 1, 1, 31, 54, 0x0 },  /* 30 ALC255_FIXUP_ACER_HEADPHONE_AND_MIC */
  { 2, 1, 32, 6, 0x0 },  /* 31 ALC255_FIXUP_XIAOMI_HEADSET_MIC */
  { 1, 0, 0, 56, 0x0 },  /* 32 ALC289_FIXUP_ASUS_GA502 */
  { 0, 1, 17, 0, 0x0 },  /* 33 ALC287_FIXUP_PREDATOR_SPK_CS35L41_I2C_2 cs35l41_fixup_i2c_two() NOT PORTED */
  { 2, 0, 0, 9, 0x0 },  /* 34 ALC269_FIXUP_DELL_M101Z */
  { 0, 1, 36, 0, 0x0 },  /* 35 ALC292_FIXUP_DELL_E7X_AAMIX alc_fixup_dell_xps13() NOT PORTED */
  { 0, 1, 37, 0, 0x0 },  /* 36 ALC292_FIXUP_DISABLE_AAMIX alc_fixup_disable_aamix() NOT PORTED */
  { 1, 1, 22, 58, 0x0 },  /* 37 ALC269_FIXUP_DELL2_MIC_NO_PRESENCE */
  { 0, 2, 35, 0, 0x0 },  /* 38 ALC292_FIXUP_DELL_E7X alc_fixup_micmute_led() NOT PORTED */
  { 2, 0, 0, 12, 0x0 },  /* 39 ALC275_FIXUP_DELL_XPS */
  { 1, 1, 41, 62, 0x0 },  /* 40 ALC290_FIXUP_SUBWOOFER */
  { 0, 0, 0, 0, 0x0 },  /* 41 ALC290_FIXUP_MONO_SPEAKERS alc290_fixup_mono_speakers() NOT PORTED */
  { 1, 1, 3, 64, 0x0 },  /* 42 ALC269_FIXUP_DELL1_MIC_NO_PRESENCE */
  { 1, 1, 44, 67, 0x0 },  /* 43 ALC290_FIXUP_SUBWOOFER_HSJACK */
  { 0, 1, 45, 0, 0x0 },  /* 44 ALC290_FIXUP_MONO_SPEAKERS_HSJACK alc290_fixup_mono_speakers() NOT PORTED */
  { 1, 1, 22, 69, 0x0 },  /* 45 ALC269_FIXUP_DELL3_MIC_NO_PRESENCE */
  { 1, 1, 3, 71, 0x0 },  /* 46 ALC293_FIXUP_DELL1_MIC_NO_PRESENCE */
  { 0, 1, 48, 0, 0x0 },  /* 47 ALC288_FIXUP_DELL_XPS_13 alc_fixup_dell_xps13() NOT PORTED */
  { 0, 1, 49, 0, 0x0 },  /* 48 ALC288_FIXUP_DISABLE_AAMIX alc_fixup_disable_aamix() NOT PORTED */
  { 1, 1, 50, 74, 0x0 },  /* 49 ALC288_FIXUP_DELL1_MIC_NO_PRESENCE */
  { 0, 1, 4, 0, 0x0 },  /* 50 ALC288_FIXUP_DELL_HEADSET_MODE alc_fixup_headset_mode_dell_alc288() NOT PORTED */
  { 1, 1, 11, 77, 0x0 },  /* 51 ALC255_FIXUP_DELL1_MIC_NO_PRESENCE */
  { 0, 1, 46, 0, 0x0 },  /* 52 ALC293_FIXUP_DISABLE_AAMIX_MULTIJACK alc_fixup_disable_aamix() NOT PORTED */
  { 1, 1, 51, 80, 0x0 },  /* 53 ALC256_FIXUP_DELL_INSPIRON_7559_SUBWOOFER */
  { 0, 1, 51, 0, 0x0 },  /* 54 ALC255_FIXUP_DELL_SPK_NOISE alc_fixup_disable_aamix() NOT PORTED */
  { 0, 1, 56, 0, 0x0 },  /* 55 ALC298_FIXUP_SPK_VOLUME alc298_fixup_speaker_volume() NOT PORTED */
  { 1, 1, 3, 82, 0x0 },  /* 56 ALC298_FIXUP_DELL_AIO_MIC_NO_PRESENCE */
  { 1, 1, 58, 84, 0x0 },  /* 57 ALC225_FIXUP_DELL_WYSE_MIC_NO_PRESENCE */
  { 0, 1, 22, 0, 0x0 },  /* 58 ALC225_FIXUP_S3_POP_NOISE alc225_fixup_s3_pop_noise() NOT PORTED */
  { 1, 1, 60, 89, 0x0 },  /* 59 ALC274_FIXUP_DELL_AIO_LINEOUT_VERB */
  { 0, 1, 42, 0, 0x0 },  /* 60 ALC274_FIXUP_DELL_BIND_DACS alc274_fixup_bind_dacs() NOT PORTED */
  { 1, 1, 15, 91, 0x0 },  /* 61 ALC255_FIXUP_DELL_HEADSET_MIC */
  { 1, 1, 51, 93, 0x0 },  /* 62 ALC255_FIXUP_DUMMY_LINEOUT_VERB */
  { 1, 1, 3, 95, 0x0 },  /* 63 ALC269_FIXUP_DELL4_MIC_NO_PRESENCE */
  { 1, 1, 65, 98, 0x0 },  /* 64 ALC225_FIXUP_DELL_WYSE_AIO_MIC_NO_PRESENCE */
  { 0, 1, 66, 0, 0x0 },  /* 65 ALC225_FIXUP_WYSE_AUTO_MUTE alc_fixup_auto_mute_via_amp() NOT PORTED */
  { 0, 1, 22, 0, 0x0 },  /* 66 ALC225_FIXUP_WYSE_DISABLE_MIC_VREF alc_fixup_disable_mic_vref() NOT PORTED */
  { 2, 1, 68, 17, 0x0 },  /* 67 ALC225_FIXUP_DELL1_MIC_NO_PRESENCE */
  { 0, 1, 42, 0, 0x0 },  /* 68 ALC225_FIXUP_DISABLE_MIC_VREF alc_fixup_disable_mic_vref() NOT PORTED */
  { 0, 1, 70, 0, 0x0 },  /* 69 ALC289_FIXUP_DUAL_SPK alc285_fixup_speaker2_to_dac1() NOT PORTED */
  { 1, 1, 63, 101, 0x0 },  /* 70 ALC289_FIXUP_DELL_SPK2 */
  { 0, 1, 51, 0, 0x0 },  /* 71 ALC236_FIXUP_DELL_AIO_HEADSET_MIC alc_fixup_no_int_mic() NOT PORTED */
  { 0, 1, 63, 0, 0x0 },  /* 72 ALC269_FIXUP_DELL4_MIC_NO_PRESENCE_QUIET alc_fixup_dell4_mic_no_presence_quiet() NOT PORTED */
  { 0, 1, 63, 0, 0x0 },  /* 73 ALC295_FIXUP_DELL_INSPIRON_TOP_SPEAKERS alc295_fixup_dell_inspiron_top_speakers() NOT PORTED */
  { 0, 1, 69, 0, 0x0 },  /* 74 ALC289_FIXUP_DELL_CS35L41_SPI_2 cs35l41_fixup_spi_two() NOT PORTED */
  { 0, 1, 76, 0, 0x0 },  /* 75 ALC289_FIXUP_RTK_AMP_DUAL_SPK alc285_fixup_speaker2_to_dac1() NOT PORTED */
  { 1, 1, 63, 103, 0x0 },  /* 76 ALC289_FIXUP_DELL_SPK1 */
  { 0, 1, 51, 0, 0x0 },  /* 77 ALC236_FIXUP_DELL_DUAL_CODECS */
  { 0, 1, 69, 0, 0x0 },  /* 78 ALC295_FIXUP_DELL_TAS2781_I2C tas2781_fixup_tias_i2c() NOT PORTED */
  { 0, 1, 51, 0, 0x0 },  /* 79 ALC236_FIXUP_DELL_HP_POP_NOISE alc285_fixup_invalidate_dacs() NOT PORTED */
  { 1, 1, 81, 105, 0x0 },  /* 80 ALC269_FIXUP_HP_DOCK_GPIO_MIC1_LED */
  { 0, 0, 0, 0, 0x0 },  /* 81 ALC269_FIXUP_HP_GPIO_MIC1_LED alc269_fixup_hp_gpio_mic1_led() NOT PORTED */
  { 1, 1, 83, 108, 0x0 },  /* 82 ALC280_FIXUP_HP_DOCK_PINS */
  { 0, 0, 0, 0, 0x0 },  /* 83 ALC280_FIXUP_HP_GPIO4 alc280_fixup_hp_gpio4() NOT PORTED */
  { 1, 1, 3, 112, 0x0 },  /* 84 ALC221_FIXUP_HP_288PRO_MIC_NO_PRESENCE */
  { 1, 1, 3, 115, 0x0 },  /* 85 ALC221_FIXUP_HP_MIC_NO_PRESENCE */
  { 1, 0, 0, 118, 0x0 },  /* 86 ALC221_FIXUP_HP_FRONT_MIC */
  { 2, 1, 88, 20, 0x0 },  /* 87 ALC295_FIXUP_HP_PAVILION_X360 */
  { 0, 1, 89, 0, 0x0 },  /* 88 ALC295_FIXUP_HP_X360 alc295_fixup_hp_top_speakers() NOT PORTED */
  { 0, 1, 90, 0, 0x0 },  /* 89 ALC269_FIXUP_HP_MUTE_LED_MIC3 alc269_fixup_hp_mute_led_mic3() NOT PORTED */
  { 0, 0, 0, 0, 0x0 },  /* 90 ALC295_FIXUP_HP_AUTO_MUTE alc_fixup_auto_mute_via_amp() NOT PORTED */
  { 1, 1, 92, 120, 0x0 },  /* 91 ALC295_FIXUP_HP_OMEN */
  { 0, 0, 0, 0, 0x0 },  /* 92 ALC269_FIXUP_HP_LINE1_MIC1_LED alc269_fixup_hp_line1_mic1_led() NOT PORTED */
  { 2, 0, 0, 25, 0x0 },  /* 93 ALC225_FIXUP_HP_AIO_SPK */
  { 0, 1, 95, 0, 0x0 },  /* 94 ALC285_FIXUP_HP_ENVY_X360 alc285_fixup_hp_envy_x360() NOT PORTED */
  { 3, 1, 96, HAK_HDA_FN_ALC_COEF_WRITE_100MS, 0x650000 },  /* 95 ALC285_FIXUP_HP_GPIO_AMP_INIT alc285_fixup_hp_gpio_amp_init() */
  { 0, 0, 0, 0, 0x0 },  /* 96 ALC285_FIXUP_HP_GPIO_LED alc285_fixup_hp_gpio_led() NOT PORTED */
  { 1, 1, 95, 133, 0x0 },  /* 97 ALC285_FIXUP_HP_ENVY_13AQ */
  { 3, 1, 99, HAK_HDA_FN_GPIO_PULSE, 0x1 },  /* 98 ALC245_FIXUP_HP_X360_AMP alc245_fixup_hp_x360_amp() */
  { 0, 0, 0, 0, 0x0 },  /* 99 ALC245_FIXUP_HP_GPIO_LED alc245_fixup_hp_gpio_led() NOT PORTED */
  { 2, 1, 101, 28, 0x0 },  /* 100 ALC285_FIXUP_HP_SPEAKERS_MICMUTE_LED */
  { 0, 0, 0, 0, 0x0 },  /* 101 ALC285_FIXUP_HP_MUTE_LED alc285_fixup_hp_mute_led() NOT PORTED */
  { 2, 1, 103, 31, 0x0 },  /* 102 ALC274_FIXUP_HP_VERBS */
  { 0, 0, 0, 0, 0x0 },  /* 103 ALC274_FIXUP_HP_89E9_GPIO alc274_fixup_hp_89e9_amp() NOT PORTED */
  { 1, 0, 0, 135, 0x0 },  /* 104 ALC236_FIXUP_HP_DMIC */
  { 1, 1, 106, 137, 0x0 },  /* 105 ALC285_FIXUP_ASUS_GA403U_HEADSET_MIC */
  { 0, 1, 107, 0, 0x0 },  /* 106 ALC285_FIXUP_ASUS_GA403U_I2C_SPEAKER2_TO_DAC1 alc285_fixup_speaker2_to_dac1() NOT PORTED */
  { 0, 0, 0, 0, 0x0 },  /* 107 ALC285_FIXUP_ASUS_GA403U alc285_fixup_asus_ga403u() NOT PORTED */
  { 1, 1, 24, 140, 0x0 },  /* 108 ALC256_FIXUP_ASUS_MIC_NO_PRESENCE */
  { 0, 1, 110, 0, 0x0 },  /* 109 ALC285_FIXUP_ASUS_GU605_SPI_SPEAKER2_TO_DAC1 alc285_fixup_speaker2_to_dac1() NOT PORTED */
  { 1, 0, 0, 142, 0x0 },  /* 110 ALC285_FIXUP_ASUS_GU605_SPI_2_HEADSET_MIC */
  { 1, 1, 24, 145, 0x0 },  /* 111 ALC256_FIXUP_ASUS_MIC */
  { 2, 1, 113, 42, 0x0 },  /* 112 ALC294_FIXUP_ASUS_SPK */
  { 1, 1, 15, 148, 0x0 },  /* 113 ALC294_FIXUP_ASUS_HEADSET_MIC */
  { 0, 1, 115, 0, 0x0 },  /* 114 ALC287_FIXUP_TAS2781_I2C tas2781_fixup_tias_i2c() NOT PORTED */
  { 0, 1, 116, 0, 0x0 },  /* 115 ALC285_FIXUP_THINKPAD_HEADSET_JACK alc_fixup_headset_jack() NOT PORTED */
  { 0, 1, 117, 0, 0x0 },  /* 116 ALC285_FIXUP_THINKPAD_X1_GEN7 alc285_fixup_thinkpad_x1_gen7() NOT PORTED */
  { 0, 1, 118, 0, 0x0 },  /* 117 ALC269_FIXUP_THINKPAD_ACPI alc_fixup_thinkpad_acpi() NOT PORTED */
  { 3, 0, 0, HAK_HDA_FN_SKU_IGNORE, 0x0 },  /* 118 ALC269_FIXUP_SKU_IGNORE alc_fixup_sku_ignore() */
  { 1, 1, 11, 150, 0x0 },  /* 119 ALC255_FIXUP_ASUS_MIC_NO_PRESENCE */
  { 0, 1, 112, 0, 0x0 },  /* 120 ALC287_FIXUP_TXNW2781_I2C_ASUS tas2781_fixup_txnw_i2c() NOT PORTED */
  { 2, 1, 27, 47, 0x0 },  /* 121 ALC233_FIXUP_EAPD_COEF_AND_MIC_NO_PRESENCE */
  { 2, 1, 123, 50, 0x0 },  /* 122 ALC269_FIXUP_ASUS_N7601ZM */
  { 1, 0, 0, 152, 0x0 },  /* 123 ALC269_FIXUP_ASUS_N7601ZM_PINS */
  { 0, 1, 113, 0, 0x0 },  /* 124 ALC294_FIXUP_ASUS_CS35L41_SPI_2 cs35l41_fixup_spi_two() NOT PORTED */
  { 1, 1, 15, 156, 0x0 },  /* 125 ALC269VB_FIXUP_ASUS_MIC_NO_PRESENCE */
  { 1, 1, 127, 158, 0x0 },  /* 126 ALC285_FIXUP_ASUS_GA605K_HEADSET_MIC */
  { 0, 0, 0, 0, 0x0 },  /* 127 ALC285_FIXUP_SPEAKER2_TO_DAC1 alc285_fixup_speaker2_to_dac1() NOT PORTED */
  { 0, 1, 129, 0, 0x0 },  /* 128 ALC269VB_FIXUP_ASUS_ZENBOOK alc269_fixup_limit_int_mic_boost() NOT PORTED */
  { 1, 0, 0, 161, 0x0 },  /* 129 ALC269VB_FIXUP_DMIC */
  { 1, 1, 131, 166, 0x0 },  /* 130 ALC285_FIXUP_ASUS_I2C_HEADSET_MIC */
  { 0, 1, 132, 0, 0x0 },  /* 131 ALC285_FIXUP_ASUS_I2C_SPEAKER2_TO_DAC1 alc285_fixup_speaker2_to_dac1() NOT PORTED */
  { 0, 0, 0, 0, 0x0 },  /* 132 ALC287_FIXUP_CS35L41_I2C_2 cs35l41_fixup_i2c_two() NOT PORTED */
  { 1, 1, 134, 169, 0x0 },  /* 133 ALC285_FIXUP_ASUS_HEADSET_MIC */
  { 0, 1, 135, 0, 0x0 },  /* 134 ALC285_FIXUP_ASUS_SPEAKER2_TO_DAC1 alc285_fixup_speaker2_to_dac1() NOT PORTED */
  { 0, 0, 0, 0, 0x0 },  /* 135 ALC245_FIXUP_CS35L41_SPI_2 cs35l41_fixup_spi_two() NOT PORTED */
  { 2, 1, 128, 57, 0x0 },  /* 136 ALC269VB_FIXUP_ASUS_ZENBOOK_UX31A */
  { 1, 0, 0, 172, 0x0 },  /* 137 ALC285_FIXUP_ASUS_G533Z_PINS */
  { 1, 0, 0, 178, 0x0 },  /* 138 ALC289_FIXUP_ASUS_ZEPHYRUS_DUAL_SPK */
  { 3, 1, 140, HAK_HDA_FN_GPIO_HIGH, 0x4 },  /* 139 ALC294_FIXUP_ASUS_DUAL_SPK alc_fixup_gpio4() */
  { 0, 1, 113, 0, 0x0 },  /* 140 ALC294_FIXUP_SPK2_TO_DAC1 alc285_fixup_speaker2_to_dac1() NOT PORTED */
  { 2, 0, 0, 60, 0x0 },  /* 141 ALC256_FIXUP_ASUS_PM3406CHA */
  { 0, 1, 32, 0, 0x0 },  /* 142 ALC289_FIXUP_ASUS_GA401 alc289_fixup_asus_ga401() NOT PORTED */
  { 0, 1, 144, 0, 0x0 },  /* 143 ALC294_FIXUP_ASUS_ALLY cs35l41_fixup_i2c_two() NOT PORTED */
  { 1, 1, 145, 181, 0x0 },  /* 144 ALC294_FIXUP_ASUS_ALLY_PINS */
  { 2, 1, 127, 63, 0x0 },  /* 145 ALC294_FIXUP_ASUS_ALLY_VERBS */
  { 1, 1, 147, 185, 0x0 },  /* 146 ALC294_FIXUP_ASUS_GX502_PINS */
  { 2, 1, 148, 76, 0x0 },  /* 147 ALC294_FIXUP_ASUS_GX502_VERBS */
  { 0, 0, 0, 0, 0x0 },  /* 148 ALC294_FIXUP_ASUS_GX502_HP alc294_fixup_gx502_hp() NOT PORTED */
  { 1, 1, 24, 189, 0x0 },  /* 149 ALC256_FIXUP_ASUS_HEADSET_MIC */
  { 2, 1, 113, 79, 0x0 },  /* 150 ALC294_FIXUP_ASUS_HPE */
  { 2, 1, 113, 82, 0x0 },  /* 151 ALC256_FIXUP_ASUS_HPE */
  { 1, 1, 3, 191, 0x0 },  /* 152 ALC295_FIXUP_ASUS_MIC_NO_PRESENCE */
  { 1, 1, 132, 193, 0x0 },  /* 153 ALC294_FIXUP_ASUS_I2C_HEADSET_MIC */
  { 1, 0, 0, 195, 0x0 },  /* 154 ALC269_FIXUP_ASUS_G73JW */
  { 1, 1, 135, 197, 0x0 },  /* 155 ALC294_FIXUP_ASUS_SPI_HEADSET_MIC */
  { 1, 0, 0, 199, 0x0 },  /* 156 ALC294_FIXUP_LENOVO_MIC_LOCATION */
  { 2, 1, 142, 85, 0x0 },  /* 157 ALC294_FIXUP_ASUS_COEF_1B */
  { 1, 1, 133, 201, 0x0 },  /* 158 ALC285_FIXUP_ASUS_SPI_REAR_SPEAKERS */
  { 1, 1, 160, 203, 0x0 },  /* 159 ALC294_FIXUP_ASUS_GU502_PINS */
  { 2, 1, 161, 88, 0x0 },  /* 160 ALC294_FIXUP_ASUS_GU502_VERBS */
  { 0, 0, 0, 0, 0x0 },  /* 161 ALC294_FIXUP_ASUS_GU502_HP alc294_fixup_gu502_hp() NOT PORTED */
  { 1, 0, 0, 207, 0x0 },  /* 162 ALC294_FIXUP_ASUS_G513_PINS */
  { 1, 1, 164, 211, 0x0 },  /* 163 ALC287_FIXUP_ASUS_ALLY_X */
  { 0, 1, 165, 0, 0x0 },  /* 164 ALC287_FIXUP_ASUS_ALLY_X_SPEAKER alc285_fixup_speaker2_to_dac1() NOT PORTED */
  { 0, 1, 166, 0, 0x0 },  /* 165 ALC287_FIXUP_ASUS_ALLY_X_I2C tas2781_fixup_tias_i2c() NOT PORTED */
  { 0, 0, 0, 0, 0x0 },  /* 166 ALC225_FIXUP_HEADSET_JACK alc_fixup_headset_jack() NOT PORTED */
  { 3, 0, 0, HAK_HDA_FN_GPIO_HIGH, 0x4 },  /* 167 ALC256_FIXUP_ASUS_AIO_GPIO2 alc_fixup_gpio4() */
  { 2, 1, 169, 92, 0x0 },  /* 168 ALC274_FIXUP_ASUS_ZEN_AIO_27 */
  { 0, 0, 0, 0, 0x0 },  /* 169 ALC2XX_FIXUP_HEADSET_MIC alc2xx_fixup_headset_mic() NOT PORTED */
  { 0, 1, 115, 0, 0x0 },  /* 170 ALC287_FIXUP_TXNW2781_I2C tas2781_fixup_txnw_i2c() NOT PORTED */
  { 1, 1, 172, 213, 0x0 },  /* 171 ALC269_FIXUP_ASUS_X101 */
  { 2, 1, 173, 105, 0x0 },  /* 172 ALC269_FIXUP_ASUS_X101_VERB */
  { 0, 0, 0, 0, 0x0 },  /* 173 ALC269_FIXUP_ASUS_X101_FUNC alc269_fixup_x101_headset_mic() NOT PORTED */
  { 0, 1, 175, 0, 0x0 },  /* 174 ALC275_FIXUP_SONY_VAIO_GPIO2 alc275_fixup_gpio4_off() NOT PORTED */
  { 4, 0, 0, 215, 0x0 },  /* 175 ALC269_FIXUP_SONY_VAIO */
  { 0, 1, 174, 0, 0x0 },  /* 176 ALC275_FIXUP_SONY_HWEQ alc269_fixup_hweq() NOT PORTED */
  { 0, 1, 175, 0, 0x0 },  /* 177 ALC275_FIXUP_SONY_DISABLE_AAMIX alc_fixup_disable_aamix() NOT PORTED */
  { 1, 1, 15, 217, 0x0 },  /* 178 ALC286_FIXUP_SONY_MIC_NO_PRESENCE */
  { 1, 0, 0, 219, 0x0 },  /* 179 ALC269_FIXUP_LIFEBOOK_HP_PIN */
  { 1, 0, 0, 221, 0x0 },  /* 180 ALC269_FIXUP_LIFEBOOK_EXTMIC */
  { 2, 0, 0, 109, 0x0 },  /* 181 ALC700_FIXUP_INTEL_REFERENCE */
  { 1, 1, 24, 223, 0x0 },  /* 182 ALC256_FIXUP_MEDION_HEADSET_NO_PRESENCE */
  { 0, 1, 184, 0, 0x0 },  /* 183 ALC298_FIXUP_SAMSUNG_AMP alc298_fixup_samsung_amp() NOT PORTED */
  { 2, 0, 0, 118, 0x0 },  /* 184 ALC298_FIXUP_SAMSUNG_HEADPHONE_VERY_QUIET */
  { 2, 0, 0, 120, 0x0 },  /* 185 ALC256_FIXUP_SAMSUNG_HEADPHONE_VERY_QUIET */
  { 1, 0, 0, 226, 0x0 },  /* 186 ALC283_FIXUP_HEADSET_MIC */
  { 1, 1, 22, 228, 0x0 },  /* 187 ALC256_FIXUP_SYSTEM76_MIC_NO_PRESENCE */
  { 1, 1, 22, 230, 0x0 },  /* 188 ALC293_FIXUP_SYSTEM76_MIC_NO_PRESENCE */
  { 0, 1, 187, 0, 0x0 },  /* 189 ALC245_FIXUP_CLEVO_NOISY_MIC alc269_fixup_limit_int_mic_boost() NOT PORTED */
  { 1, 0, 0, 232, 0x0 },  /* 190 ALC269_FIXUP_DMIC */
  { 1, 1, 192, 237, 0x0 },  /* 191 ALC269_FIXUP_VAIO_VJFH52_MIC_NO_PRESENCE */
  { 0, 0, 0, 0, 0x0 },  /* 192 ALC269_FIXUP_LIMIT_INT_MIC_BOOST alc269_fixup_limit_int_mic_boost() NOT PORTED */
  { 1, 1, 192, 240, 0x0 },  /* 193 ALC256_FIXUP_VAIO_RPL_MIC_NO_PRESENCE */
  { 0, 1, 195, 0, 0x0 },  /* 194 ALC233_FIXUP_LENOVO_MULTI_CODECS alc233_alc662_fixup_lenovo_dual_codecs() NOT PORTED */
  { 3, 0, 0, HAK_HDA_FN_GPIO_HIGH, 0x2 },  /* 195 ALC269_FIXUP_GPIO2 alc_fixup_gpio2() */
  { 0, 1, 186, 0, 0x0 },  /* 196 ALC623_FIXUP_LENOVO_THINKSTATION_P340 alc_fixup_no_shutup() NOT PORTED */
  { 1, 1, 198, 243, 0x0 },  /* 197 ALC269_FIXUP_LENOVO_DOCK */
  { 0, 1, 117, 0, 0x0 },  /* 198 ALC269_FIXUP_PINCFG_NO_HP_TO_LINEOUT alc269_fixup_pincfg_no_hp_to_lineout() NOT PORTED */
  { 0, 1, 197, 0, 0x0 },  /* 199 ALC269_FIXUP_LENOVO_DOCK_LIMIT_BOOST alc269_fixup_limit_int_mic_boost() NOT PORTED */
  { 0, 1, 117, 0, 0x0 },  /* 200 ALC269_FIXUP_THINKPAD_LIMIT_INT_MIC_BOOST alc269_fixup_limit_int_mic_boost() NOT PORTED */
  { 0, 1, 202, 0, 0x0 },  /* 201 ALC298_FIXUP_TPT470_DOCK alc_fixup_tpt470_dacs() NOT PORTED */
  { 0, 1, 203, 0, 0x0 },  /* 202 ALC298_FIXUP_TPT470_DOCK_FIX alc_fixup_tpt470_dock() NOT PORTED */
  { 0, 1, 117, 0, 0x0 },  /* 203 ALC293_FIXUP_LENOVO_SPK_NOISE alc_fixup_disable_aamix() NOT PORTED */
  { 0, 1, 203, 0, 0x0 },  /* 204 ALC292_FIXUP_TPT460 alc_fixup_tpt440_dock() NOT PORTED */
  { 0, 1, 117, 0, 0x0 },  /* 205 ALC285_FIXUP_THINKPAD_NO_BASS_SPK_HEADSET_JACK alc_fixup_headset_jack() NOT PORTED */
  { 0, 1, 207, 0, 0x0 },  /* 206 ALC287_FIXUP_MG_RTKC_CSAMP_CS35L41_I2C_THINKPAD alc287_fixup_tb_vmaster_led() NOT PORTED */
  { 0, 1, 205, 0, 0x0 },  /* 207 ALC287_FIXUP_CS35L41_I2C_2_THINKPAD_ACPI cs35l41_fixup_i2c_two() NOT PORTED */
  { 0, 1, 117, 0, 0x0 },  /* 208 ALC287_FIXUP_LENOVO_THKPAD_WH_ALC1318 alc287_fixup_lenovo_thinkpad_with_alc1318() NOT PORTED */
  { 3, 1, 210, HAK_HDA_FN_PIN17_BASS_SPEAKER, 0x0 },  /* 209 ALC287_FIXUP_YOGA9_14IAP7_BASS_SPK_PIN alc287_fixup_yoga9_14iap7_bass_spk_pin() */
  { 2, 0, 0, 123, 0x0 },  /* 210 ALC287_FIXUP_YOGA9_14IAP7_BASS_SPK */
  { 2, 1, 3, 168, 0x0 },  /* 211 ALC287_FIXUP_YOGA7_14ITL_SPEAKERS */
  { 2, 1, 213, 193, 0x0 },  /* 212 ALC287_FIXUP_LEGION_15IMHG05_SPEAKERS */
  { 0, 1, 3, 0, 0x0 },  /* 213 ALC287_FIXUP_LEGION_15IMHG05_AUTOMUTE alc287_fixup_legion_15imhg05_speakers() NOT PORTED */
  { 2, 1, 3, 218, 0x0 },  /* 214 ALC287_FIXUP_13S_GEN2_SPEAKERS */
  { 0, 1, 216, 0, 0x0 },  /* 215 ALC285_FIXUP_LEGION_Y9000X_SPEAKERS alc285_fixup_ideapad_s740_coef() NOT PORTED */
  { 0, 1, 117, 0, 0x0 },  /* 216 ALC285_FIXUP_LEGION_Y9000X_AUTOMUTE alc287_fixup_legion_15imhg05_speakers() NOT PORTED */
  { 0, 1, 117, 0, 0x0 },  /* 217 ALC285_FIXUP_IDEAPAD_S740_COEF alc285_fixup_ideapad_s740_coef() NOT PORTED */
  { 3, 1, 132, HAK_HDA_FN_PIN17_BASS_SPEAKER, 0x0 },  /* 218 ALC287_FIXUP_YOGA9_14IMH9_BASS_SPK_PIN alc287_fixup_yoga9_14iap7_bass_spk_pin() */
  { 0, 1, 117, 0, 0x0 },  /* 219 ALC285_FIXUP_YOGA_SPEAKER2_TO_DAC1 alc285_fixup_speaker2_to_dac1() NOT PORTED */
  { 0, 1, 115, 0, 0x0 },  /* 220 ALC287_FIXUP_YOGA7_14ARB7_I2C yoga7_14arb7_fixup_i2c() NOT PORTED */
  { 0, 1, 222, 0, 0x0 },  /* 221 ALC287_FIXUP_LENOVO_YOGA_PRO7 alc_fixup_yoga_pro7_audio() NOT PORTED */
  { 0, 1, 117, 0, 0x0 },  /* 222 ALC269_FIXUP_LENOVO_XPAD_ACPI alc_fixup_ideapad_acpi() NOT PORTED */
  { 0, 1, 117, 0, 0x0 },  /* 223 ALC269_FIXUP_DMIC_THINKPAD_ACPI alc_fixup_inv_dmic() NOT PORTED */
  { 0, 1, 209, 0, 0x0 },  /* 224 ALC287_FIXUP_YOGA9_14IAP7_BASS_SPK_PIN_HEADSET alc_fixup_headset_jack() NOT PORTED */
  { 0, 1, 226, 0, 0x0 },  /* 225 ALC236_FIXUP_LENOVO_INV_DMIC alc_fixup_inv_dmic() NOT PORTED */
  { 2, 1, 192, 233, 0x0 },  /* 226 ALC283_FIXUP_INT_MIC */
  { 0, 1, 170, 0, 0x0 },  /* 227 ALC287_FIXUP_YOGA9_SPEAKER2_TO_DAC1 alc285_fixup_speaker2_to_dac1() NOT PORTED */
  { 0, 1, 221, 0, 0x0 },  /* 228 ALC287_FIXUP_LENOVO_XPAD_HEADSET_JACK alc_fixup_headset_jack() NOT PORTED */
  { 2, 0, 0, 236, 0x0 },  /* 229 ALC269_FIXUP_LENOVO_EAPD */
  { 1, 1, 4, 246, 0x0 },  /* 230 ALC256_FIXUP_HUAWEI_MACH_WX9_PINS */
  { 1, 1, 232, 257, 0x0 },  /* 231 ALC298_FIXUP_RAZER_BLADE16_2025_PINS */
  { 0, 0, 0, 0, 0x0 },  /* 232 ALC298_FIXUP_RAZER_BLADE16_2025 alc298_fixup_razer_blade16_2025() NOT PORTED */
  { 1, 0, 190, 260, 0x0 },  /* 233 ALC269_FIXUP_CZC_B20 */
  { 1, 0, 190, 271, 0x0 },  /* 234 ALC269_FIXUP_CZC_TMI */
  { 1, 0, 190, 283, 0x0 },  /* 235 ALC269_FIXUP_CZC_L101 */
  { 1, 0, 0, 295, 0x0 },  /* 236 ALC269VB_FIXUP_ORDISSIMO_EVE2 */
  { 1, 0, 190, 299, 0x0 },  /* 237 ALC269_FIXUP_LEMOTE_A1802 */
  { 1, 0, 190, 311, 0x0 },  /* 238 ALC269_FIXUP_LEMOTE_A190X */
  { 1, 1, 3, 317, 0x0 },  /* 239 ALC298_FIXUP_DELL1_MIC_NO_PRESENCE */
  { 1, 0, 0, 320, 0x0 },  /* 240 ALC233_FIXUP_MEDION_MTL_SPK */
  { 1, 0, 0, 322, 0x0 },  /* 241 ALC233_FIXUP_WUJIE_SPEAKERS */
  { 1, 0, 0, 324, 0x0 },  /* 242 ALC256_FIXUP_HONOR_MRB_XXX_M1020_AUDIO */
  { 1, 0, 0, 329, 0x0 },  /* 243 ALC256_FIXUP_IPASON_SMARTBOOK_S1 */
  { 1, 1, 192, 331, 0x0 },  /* 244 ALC269VB_FIXUP_INFINIX_ZERO_BOOK_13 */
  { 1, 0, 0, 334, 0x0 },  /* 245 ALC269VB_FIXUP_CHUWI_COREBOOK_XPRO */
  { 1, 0, 0, 337, 0x0 },  /* 246 ALC269VB_FIXUP_INFINIX_INBOOK_X3_SLIM */
  { 0, 1, 20, 0, 0x0 },  /* 247 ALC269_FIXUP_POSITIVO_P15X_HEADSET_MIC alc269_fixup_limit_int_mic_boost() NOT PORTED */
  { 1, 1, 192, 340, 0x0 },  /* 248 ALC269VC_FIXUP_INFINIX_Y4_MAX */
  { 1, 0, 0, 342, 0x0 },  /* 249 ALC269VC_FIXUP_LUNNEN_GROUND_14 */
  { 1, 1, 3, 344, 0x0 },  /* 250 ALC256_FIXUP_INTEL_NUC8_RUGGED */
  { 1, 1, 3, 346, 0x0 },  /* 251 ALC256_FIXUP_INTEL_NUC10 */
  { 1, 1, 22, 348, 0x0 },  /* 252 ALC295_FIXUP_FRAMEWORK_LAPTOP_MIC_NO_PRESENCE */
  { 0, 1, 252, 0, 0x0 },  /* 253 ALC295_FIXUP_FRAMEWORK_LAPTOP_LIMIT_INT_MIC_BOOST alc269_fixup_limit_int_mic_boost() NOT PORTED */
};
static CONST HAK_HDA_QUIRK mHakQuirks_alc269[] = {
  { 0x1025, 0x0283, 0xffff, 0x7fff },  /* Acer TravelMate 8371 -> ALC269_FIXUP_INV_DMIC (nothing to do) */
  { 0x1025, 0x029b, 0xffff, 0x7fff },  /* Acer 1810TZ -> ALC269_FIXUP_INV_DMIC (nothing to do) */
  { 0x1025, 0x0349, 0xffff, 0x7fff },  /* Acer AOD260 -> ALC269_FIXUP_INV_DMIC (nothing to do) */
  { 0x1025, 0x047c, 0xffff, 0x0000 },  /* Acer AC700 -> ALC269_FIXUP_ACER_AC700 */
  { 0x1025, 0x072d, 0xffff, 0x0002 },  /* Acer Aspire V5-571G -> ALC269_FIXUP_ASPIRE_HEADSET_MIC */
  { 0x1025, 0x0740, 0xffff, 0x0005 },  /* Acer AO725 -> ALC271_FIXUP_HP_GATE_MIC_JACK */
  { 0x1025, 0x0742, 0xffff, 0x0005 },  /* Acer AO756 -> ALC271_FIXUP_HP_GATE_MIC_JACK */
  { 0x1025, 0x0762, 0xffff, 0x0007 },  /* Acer Aspire E1-472 -> ALC271_FIXUP_HP_GATE_MIC_JACK_E1_572 */
  { 0x1025, 0x0775, 0xffff, 0x0007 },  /* Acer Aspire E1-572 -> ALC271_FIXUP_HP_GATE_MIC_JACK_E1_572 */
  { 0x1025, 0x079b, 0xffff, 0x0008 },  /* Acer Aspire V5-573G -> ALC282_FIXUP_ASPIRE_V5_PINS */
  { 0x1025, 0x080d, 0xffff, 0x0002 },  /* Acer Aspire V5-122P -> ALC269_FIXUP_ASPIRE_HEADSET_MIC */
  { 0x1025, 0x0840, 0xffff, 0x7fff },  /* Acer Aspire E1 -> ALC269VB_FIXUP_ASPIRE_E1_COEF (nothing to do) */
  { 0x1025, 0x0943, 0xffff, 0x0002 },  /* Acer Aspire V3-572G -> ALC269_FIXUP_ASPIRE_HEADSET_MIC */
  { 0x1025, 0x100c, 0xffff, 0x0009 },  /* Acer Aspire E5-574G -> ALC255_FIXUP_ACER_LIMIT_INT_MIC_BOOST */
  { 0x1025, 0x101c, 0xffff, 0x000c },  /* Acer Veriton N2510G -> ALC269_FIXUP_LIFEBOOK */
  { 0x1025, 0x102b, 0xffff, 0x000e },  /* Acer Aspire C24-860 -> ALC286_FIXUP_ACER_AIO_MIC_NO_PRESENCE */
  { 0x1025, 0x1065, 0xffff, 0x0010 },  /* Acer Aspire C20-820 -> ALC269VC_FIXUP_ACER_HEADSET_MIC */
  { 0x1025, 0x106d, 0xffff, 0x7fff },  /* Acer Cloudbook 14 -> ALC283_FIXUP_CHROME_BOOK (nothing to do) */
  { 0x1025, 0x1094, 0xffff, 0x0009 },  /* Acer Aspire E5-575T -> ALC255_FIXUP_ACER_LIMIT_INT_MIC_BOOST */
  { 0x1025, 0x1099, 0xffff, 0x000a },  /* Acer Aspire E5-523G -> ALC255_FIXUP_ACER_MIC_NO_PRESENCE */
  { 0x1025, 0x110e, 0xffff, 0x000a },  /* Acer Aspire ES1-432 -> ALC255_FIXUP_ACER_MIC_NO_PRESENCE */
  { 0x1025, 0x1166, 0xffff, 0x000c },  /* Acer Veriton N4640G -> ALC269_FIXUP_LIFEBOOK */
  { 0x1025, 0x1167, 0xffff, 0x000c },  /* Acer Veriton N6640G -> ALC269_FIXUP_LIFEBOOK */
  { 0x1025, 0x1177, 0xffff, 0x0011 },  /* Acer Predator G9-593 -> ALC255_FIXUP_PREDATOR_SUBWOOFER */
  { 0x1025, 0x1178, 0xffff, 0x0011 },  /* Acer Predator G9-593 -> ALC255_FIXUP_PREDATOR_SUBWOOFER */
  { 0x1025, 0x1246, 0xffff, 0x0012 },  /* Acer Predator Helios 500 -> ALC299_FIXUP_PREDATOR_SPK */
  { 0x1025, 0x1247, 0xffff, 0x0013 },  /* Acer vCopperbox -> ALC269VC_FIXUP_ACER_VCOPPERBOX_PINS */
  { 0x1025, 0x1248, 0xffff, 0x0014 },  /* Acer Veriton N4660G -> ALC269VC_FIXUP_ACER_MIC_NO_PRESENCE */
  { 0x1025, 0x1269, 0xffff, 0x0015 },  /* Acer SWIFT SF314-54 -> ALC256_FIXUP_ACER_HEADSET_MIC */
  { 0x1025, 0x126a, 0xffff, 0x0017 },  /* Acer Swift SF114-32 -> ALC256_FIXUP_ACER_MIC_NO_PRESENCE */
  { 0x1025, 0x128f, 0xffff, 0x0019 },  /* Acer Veriton Z6860G -> ALC286_FIXUP_ACER_AIO_HEADSET_MIC */
  { 0x1025, 0x1290, 0xffff, 0x0019 },  /* Acer Veriton Z4860G -> ALC286_FIXUP_ACER_AIO_HEADSET_MIC */
  { 0x1025, 0x1291, 0xffff, 0x0019 },  /* Acer Veriton Z4660G -> ALC286_FIXUP_ACER_AIO_HEADSET_MIC */
  { 0x1025, 0x129c, 0xffff, 0x0015 },  /* Acer SWIFT SF314-55 -> ALC256_FIXUP_ACER_HEADSET_MIC */
  { 0x1025, 0x129d, 0xffff, 0x0017 },  /* Acer SWIFT SF313-51 -> ALC256_FIXUP_ACER_MIC_NO_PRESENCE */
  { 0x1025, 0x1300, 0xffff, 0x0017 },  /* Acer SWIFT SF314-56 -> ALC256_FIXUP_ACER_MIC_NO_PRESENCE */
  { 0x1025, 0x1308, 0xffff, 0x0019 },  /* Acer Aspire Z24-890 -> ALC286_FIXUP_ACER_AIO_HEADSET_MIC */
  { 0x1025, 0x132a, 0xffff, 0x001a },  /* Acer TravelMate B114-21 -> ALC233_FIXUP_ACER_HEADSET_MIC */
  { 0x1025, 0x1330, 0xffff, 0x001c },  /* Acer TravelMate X514-51T -> ALC255_FIXUP_ACER_HEADSET_MIC */
  { 0x1025, 0x1360, 0xffff, 0x000a },  /* Acer Aspire A115 -> ALC255_FIXUP_ACER_MIC_NO_PRESENCE */
  { 0x1025, 0x141f, 0xffff, 0x000a },  /* Acer Spin SP513-54N -> ALC255_FIXUP_ACER_MIC_NO_PRESENCE */
  { 0x1025, 0x142b, 0xffff, 0x000a },  /* Acer Swift SF314-42 -> ALC255_FIXUP_ACER_MIC_NO_PRESENCE */
  { 0x1025, 0x1430, 0xffff, 0x0017 },  /* Acer TravelMate B311R-31 -> ALC256_FIXUP_ACER_MIC_NO_PRESENCE */
  { 0x1025, 0x1466, 0xffff, 0x001e },  /* Acer Aspire A515-56 -> ALC255_FIXUP_ACER_HEADPHONE_AND_MIC */
  { 0x1025, 0x151e, 0xffff, 0x7fff },  /* Acer Nitro AN515-45 -> ALC2XX_FIXUP_HEADSET_MIC (nothing to do) */
  { 0x1025, 0x1534, 0xffff, 0x000a },  /* Acer Predator PH315-54 -> ALC255_FIXUP_ACER_MIC_NO_PRESENCE */
  { 0x1025, 0x1539, 0xffff, 0x7fff },  /* Acer Nitro 5 AN515-57 -> ALC2XX_FIXUP_HEADSET_MIC (nothing to do) */
  { 0x1025, 0x159c, 0xffff, 0x7fff },  /* Acer Nitro 5 AN515-58 -> ALC287_FIXUP_ACER_MICMUTE_LED (nothing to do) */
  { 0x1025, 0x1597, 0xffff, 0x7fff },  /* Acer Nitro 5 AN517-55 -> ALC2XX_FIXUP_HEADSET_MIC (nothing to do) */
  { 0x1025, 0x159e, 0xffff, 0x7fff },  /* Acer Nitro 5 AN515-46 -> ALC2XX_FIXUP_HEADSET_MIC (nothing to do) */
  { 0x1025, 0x160e, 0xffff, 0x7fff },  /* Acer PT316-51S -> ALC2XX_FIXUP_HEADSET_MIC (nothing to do) */
  { 0x1025, 0x1616, 0xffff, 0x7fff },  /* Acer Aspire A515-57 -> ALC256_FIXUP_ACER_COLDBOOT (nothing to do) */
  { 0x1025, 0x161f, 0xffff, 0x0017 },  /* Acer S40-54 -> ALC256_FIXUP_ACER_MIC_NO_PRESENCE */
  { 0x1025, 0x1640, 0xffff, 0x7fff },  /* Acer Aspire A315-44P -> ALC256_FIXUP_ACER_SFG16_MICMUTE_LED (nothing to do) */
  { 0x1025, 0x166c, 0xffff, 0x7fff },  /* Acer Predator PH16-71 -> ALC2XX_FIXUP_HEADSET_MIC (nothing to do) */
  { 0x1025, 0x1679, 0xffff, 0x7fff },  /* Acer Nitro 16 AN16-41 -> ALC2XX_FIXUP_HEADSET_MIC (nothing to do) */
  { 0x1025, 0x169a, 0xffff, 0x7fff },  /* Acer Swift SFG16 -> ALC256_FIXUP_ACER_SFG16_MICMUTE_LED (nothing to do) */
  { 0x1025, 0x171e, 0xffff, 0x7fff },  /* Acer Nitro ANV15-51 -> ALC245_FIXUP_ACER_MICMUTE_LED (nothing to do) */
  { 0x1025, 0x172c, 0xffff, 0x7fff },  /* Acer Nitro ANV16-41 -> ALC2XX_FIXUP_HEADSET_MIC (nothing to do) */
  { 0x1025, 0x1731, 0xffff, 0x7fff },  /* Acer Predator PHN16-72 -> ALC2XX_FIXUP_HEADSET_MIC (nothing to do) */
  { 0x1025, 0x173a, 0xffff, 0x7fff },  /* Acer Swift SFG14-73 -> ALC245_FIXUP_ACER_MICMUTE_LED (nothing to do) */
  { 0x1025, 0x173b, 0xffff, 0x7fff },  /* Acer Swift SFG16-72 -> ALC245_FIXUP_ACER_MICMUTE_LED (nothing to do) */
  { 0x1025, 0x1758, 0xffff, 0x7fff },  /* Acer Nitro ANV15-41 -> ALC245_FIXUP_ACER_MICMUTE_LED (nothing to do) */
  { 0x1025, 0x1826, 0xffff, 0x0021 },  /* Acer Helios ZPC -> ALC287_FIXUP_PREDATOR_SPK_CS35L41_I2C_2 */
  { 0x1025, 0x182c, 0xffff, 0x0021 },  /* Acer Helios ZPD -> ALC287_FIXUP_PREDATOR_SPK_CS35L41_I2C_2 */
  { 0x1025, 0x1844, 0xffff, 0x0021 },  /* Acer Helios ZPS -> ALC287_FIXUP_PREDATOR_SPK_CS35L41_I2C_2 */
  { 0x1025, 0x1909, 0xffff, 0x7fff },  /* Acer Nitro ANV16-42 -> ALC2XX_FIXUP_HEADSET_MIC (nothing to do) */
  { 0x1028, 0x0470, 0xffff, 0x0022 },  /* Dell M101z -> ALC269_FIXUP_DELL_M101Z */
  { 0x1028, 0x053c, 0xffff, 0x0026 },  /* Dell Latitude E5430 -> ALC292_FIXUP_DELL_E7X */
  { 0x1028, 0x054b, 0xffff, 0x0027 },  /* Dell XPS one 2710 -> ALC275_FIXUP_DELL_XPS */
  { 0x1028, 0x05bd, 0xffff, 0x0026 },  /* Dell Latitude E6440 -> ALC292_FIXUP_DELL_E7X */
  { 0x1028, 0x05be, 0xffff, 0x0026 },  /* Dell Latitude E6540 -> ALC292_FIXUP_DELL_E7X */
  { 0x1028, 0x05ca, 0xffff, 0x0026 },  /* Dell Latitude E7240 -> ALC292_FIXUP_DELL_E7X */
  { 0x1028, 0x05cb, 0xffff, 0x0026 },  /* Dell Latitude E7440 -> ALC292_FIXUP_DELL_E7X */
  { 0x1028, 0x05da, 0xffff, 0x0028 },  /* Dell Vostro 5460 -> ALC290_FIXUP_SUBWOOFER */
  { 0x1028, 0x05f4, 0xffff, 0x002a },  /* Dell -> ALC269_FIXUP_DELL1_MIC_NO_PRESENCE */
  { 0x1028, 0x05f5, 0xffff, 0x002a },  /* Dell -> ALC269_FIXUP_DELL1_MIC_NO_PRESENCE */
  { 0x1028, 0x05f6, 0xffff, 0x002a },  /* Dell -> ALC269_FIXUP_DELL1_MIC_NO_PRESENCE */
  { 0x1028, 0x0604, 0xffff, 0x7fff },  /* Dell Venue 11 Pro 7130 -> ALC283_FIXUP_DELL_HP_RESUME (nothing to do) */
  { 0x1028, 0x0615, 0xffff, 0x002b },  /* Dell Vostro 5470 -> ALC290_FIXUP_SUBWOOFER_HSJACK */
  { 0x1028, 0x0616, 0xffff, 0x002b },  /* Dell Vostro 5470 -> ALC290_FIXUP_SUBWOOFER_HSJACK */
  { 0x1028, 0x062c, 0xffff, 0x0026 },  /* Dell Latitude E5550 -> ALC292_FIXUP_DELL_E7X */
  { 0x1028, 0x062e, 0xffff, 0x0026 },  /* Dell Latitude E7450 -> ALC292_FIXUP_DELL_E7X */
  { 0x1028, 0x0638, 0xffff, 0x002c },  /* Dell Inspiron 5439 -> ALC290_FIXUP_MONO_SPEAKERS_HSJACK */
  { 0x1028, 0x064a, 0xffff, 0x002e },  /* Dell -> ALC293_FIXUP_DELL1_MIC_NO_PRESENCE */
  { 0x1028, 0x064b, 0xffff, 0x002e },  /* Dell -> ALC293_FIXUP_DELL1_MIC_NO_PRESENCE */
  { 0x1028, 0x0665, 0xffff, 0x002f },  /* Dell XPS 13 -> ALC288_FIXUP_DELL_XPS_13 */
  { 0x1028, 0x0669, 0xffff, 0x0033 },  /* Dell Optiplex 9020m -> ALC255_FIXUP_DELL1_MIC_NO_PRESENCE */
  { 0x1028, 0x069a, 0xffff, 0x002b },  /* Dell Vostro 5480 -> ALC290_FIXUP_SUBWOOFER_HSJACK */
  { 0x1028, 0x06c7, 0xffff, 0x0033 },  /* Dell -> ALC255_FIXUP_DELL1_MIC_NO_PRESENCE */
  { 0x1028, 0x06d9, 0xffff, 0x002e },  /* Dell -> ALC293_FIXUP_DELL1_MIC_NO_PRESENCE */
  { 0x1028, 0x06da, 0xffff, 0x002e },  /* Dell -> ALC293_FIXUP_DELL1_MIC_NO_PRESENCE */
  { 0x1028, 0x06db, 0xffff, 0x0034 },  /* Dell -> ALC293_FIXUP_DISABLE_AAMIX_MULTIJACK */
  { 0x1028, 0x06dd, 0xffff, 0x0034 },  /* Dell -> ALC293_FIXUP_DISABLE_AAMIX_MULTIJACK */
  { 0x1028, 0x06de, 0xffff, 0x0034 },  /* Dell -> ALC293_FIXUP_DISABLE_AAMIX_MULTIJACK */
  { 0x1028, 0x06df, 0xffff, 0x0034 },  /* Dell -> ALC293_FIXUP_DISABLE_AAMIX_MULTIJACK */
  { 0x1028, 0x06e0, 0xffff, 0x0034 },  /* Dell -> ALC293_FIXUP_DISABLE_AAMIX_MULTIJACK */
  { 0x1028, 0x0706, 0xffff, 0x0035 },  /* Dell Inspiron 7559 -> ALC256_FIXUP_DELL_INSPIRON_7559_SUBWOOFER */
  { 0x1028, 0x0725, 0xffff, 0x0036 },  /* Dell Inspiron 3162 -> ALC255_FIXUP_DELL_SPK_NOISE */
  { 0x1028, 0x0738, 0xffff, 0x7fff },  /* Dell Precision 5820 -> ALC269_FIXUP_NO_SHUTUP (nothing to do) */
  { 0x1028, 0x075c, 0xffff, 0x0037 },  /* Dell XPS 27 7760 -> ALC298_FIXUP_SPK_VOLUME */
  { 0x1028, 0x075d, 0xffff, 0x0037 },  /* Dell AIO -> ALC298_FIXUP_SPK_VOLUME */
  { 0x1028, 0x0798, 0xffff, 0x0035 },  /* Dell Inspiron 17 7000 Gaming -> ALC256_FIXUP_DELL_INSPIRON_7559_SUBWOOFER */
  { 0x1028, 0x07b0, 0xffff, 0x7fff },  /* Dell Precision 7520 -> ALC295_FIXUP_DISABLE_DAC3 (nothing to do) */
  { 0x1028, 0x080c, 0xffff, 0x0039 },  /* Dell WYSE -> ALC225_FIXUP_DELL_WYSE_MIC_NO_PRESENCE */
  { 0x1028, 0x084b, 0xffff, 0x003b },  /* Dell -> ALC274_FIXUP_DELL_AIO_LINEOUT_VERB */
  { 0x1028, 0x084e, 0xffff, 0x003b },  /* Dell -> ALC274_FIXUP_DELL_AIO_LINEOUT_VERB */
  { 0x1028, 0x0871, 0xffff, 0x003d },  /* Dell Precision 3630 -> ALC255_FIXUP_DELL_HEADSET_MIC */
  { 0x1028, 0x0872, 0xffff, 0x003d },  /* Dell Precision 3630 -> ALC255_FIXUP_DELL_HEADSET_MIC */
  { 0x1028, 0x0873, 0xffff, 0x003e },  /* Dell Precision 3930 -> ALC255_FIXUP_DUMMY_LINEOUT_VERB */
  { 0x1028, 0x0879, 0xffff, 0x003f },  /* Dell Latitude 5420 Rugged -> ALC269_FIXUP_DELL4_MIC_NO_PRESENCE */
  { 0x1028, 0x08ad, 0xffff, 0x0040 },  /* Dell WYSE AIO -> ALC225_FIXUP_DELL_WYSE_AIO_MIC_NO_PRESENCE */
  { 0x1028, 0x08ae, 0xffff, 0x0043 },  /* Dell WYSE NB -> ALC225_FIXUP_DELL1_MIC_NO_PRESENCE */
  { 0x1028, 0x0935, 0xffff, 0x003b },  /* Dell -> ALC274_FIXUP_DELL_AIO_LINEOUT_VERB */
  { 0x1028, 0x097d, 0xffff, 0x0045 },  /* Dell Precision -> ALC289_FIXUP_DUAL_SPK */
  { 0x1028, 0x097e, 0xffff, 0x0045 },  /* Dell Precision -> ALC289_FIXUP_DUAL_SPK */
  { 0x1028, 0x098d, 0xffff, 0x001b },  /* Dell Precision -> ALC233_FIXUP_ASUS_MIC_NO_PRESENCE */
  { 0x1028, 0x09bf, 0xffff, 0x001b },  /* Dell Precision -> ALC233_FIXUP_ASUS_MIC_NO_PRESENCE */
  { 0x1028, 0x0a2e, 0xffff, 0x0047 },  /* Dell -> ALC236_FIXUP_DELL_AIO_HEADSET_MIC */
  { 0x1028, 0x0a30, 0xffff, 0x0047 },  /* Dell -> ALC236_FIXUP_DELL_AIO_HEADSET_MIC */
  { 0x1028, 0x0a38, 0xffff, 0x0048 },  /* Dell Latitude 7520 -> ALC269_FIXUP_DELL4_MIC_NO_PRESENCE_QUIET */
  { 0x1028, 0x0a58, 0xffff, 0x003d },  /* Dell -> ALC255_FIXUP_DELL_HEADSET_MIC */
  { 0x1028, 0x0a61, 0xffff, 0x0045 },  /* Dell XPS 15 9510 -> ALC289_FIXUP_DUAL_SPK */
  { 0x1028, 0x0a62, 0xffff, 0x0045 },  /* Dell Precision 5560 -> ALC289_FIXUP_DUAL_SPK */
  { 0x1028, 0x0a9d, 0xffff, 0x003f },  /* Dell Latitude 5430 -> ALC269_FIXUP_DELL4_MIC_NO_PRESENCE */
  { 0x1028, 0x0a9e, 0xffff, 0x003f },  /* Dell Latitude 5430 -> ALC269_FIXUP_DELL4_MIC_NO_PRESENCE */
  { 0x1028, 0x0b19, 0xffff, 0x0045 },  /* Dell XPS 15 9520 -> ALC289_FIXUP_DUAL_SPK */
  { 0x1028, 0x0b1a, 0xffff, 0x0045 },  /* Dell Precision 5570 -> ALC289_FIXUP_DUAL_SPK */
  { 0x1028, 0x0b27, 0xffff, 0x7fff },  /* Dell -> ALC245_FIXUP_CS35L41_SPI_2 (nothing to do) */
  { 0x1028, 0x0b28, 0xffff, 0x7fff },  /* Dell -> ALC245_FIXUP_CS35L41_SPI_2 (nothing to do) */
  { 0x1028, 0x0b37, 0xffff, 0x0049 },  /* Dell Inspiron 16 Plus 7620 2-in-1 -> ALC295_FIXUP_DELL_INSPIRON_TOP_SPEAKERS */
  { 0x1028, 0x0b71, 0xffff, 0x0049 },  /* Dell Inspiron 16 Plus 7620 -> ALC295_FIXUP_DELL_INSPIRON_TOP_SPEAKERS */
  { 0x1028, 0x0beb, 0xffff, 0x004a },  /* Dell XPS 15 9530 (2023) -> ALC289_FIXUP_DELL_CS35L41_SPI_2 */
  { 0x1028, 0x0c03, 0xffff, 0x003f },  /* Dell Precision 5340 -> ALC269_FIXUP_DELL4_MIC_NO_PRESENCE */
  { 0x1028, 0x0c0b, 0xffff, 0x004b },  /* Dell Oasis 14 RPL-P -> ALC289_FIXUP_RTK_AMP_DUAL_SPK */
  { 0x1028, 0x0c0d, 0xffff, 0x004b },  /* Dell Oasis -> ALC289_FIXUP_RTK_AMP_DUAL_SPK */
  { 0x1028, 0x0c0e, 0xffff, 0x004b },  /* Dell Oasis 16 -> ALC289_FIXUP_RTK_AMP_DUAL_SPK */
  { 0x1028, 0x0c19, 0xffff, 0x004d },  /* Dell Precision 3340 -> ALC236_FIXUP_DELL_DUAL_CODECS */
  { 0x1028, 0x0c1a, 0xffff, 0x004d },  /* Dell Precision 3340 -> ALC236_FIXUP_DELL_DUAL_CODECS */
  { 0x1028, 0x0c1b, 0xffff, 0x004d },  /* Dell Precision 3440 -> ALC236_FIXUP_DELL_DUAL_CODECS */
  { 0x1028, 0x0c1c, 0xffff, 0x004d },  /* Dell Precision 3540 -> ALC236_FIXUP_DELL_DUAL_CODECS */
  { 0x1028, 0x0c1d, 0xffff, 0x004d },  /* Dell Precision 3440 -> ALC236_FIXUP_DELL_DUAL_CODECS */
  { 0x1028, 0x0c1e, 0xffff, 0x004d },  /* Dell Precision 3540 -> ALC236_FIXUP_DELL_DUAL_CODECS */
  { 0x1028, 0x0c28, 0xffff, 0x0049 },  /* Dell Inspiron 16 Plus 7630 -> ALC295_FIXUP_DELL_INSPIRON_TOP_SPEAKERS */
  { 0x1028, 0x0c4d, 0xffff, 0x7fff },  /* Dell -> ALC287_FIXUP_CS35L41_I2C_4 (nothing to do) */
  { 0x1028, 0x0c90, 0xffff, 0x0045 },  /* Alienware x16 R2 -> ALC289_FIXUP_DUAL_SPK */
  { 0x1028, 0x0c94, 0xffff, 0x004e },  /* Dell Polaris 3 metal -> ALC295_FIXUP_DELL_TAS2781_I2C */
  { 0x1028, 0x0c96, 0xffff, 0x004e },  /* Dell Polaris 2in1 -> ALC295_FIXUP_DELL_TAS2781_I2C */
  { 0x1028, 0x0cbd, 0xffff, 0x004a },  /* Dell Oasis 13 CS MTL-U -> ALC289_FIXUP_DELL_CS35L41_SPI_2 */
  { 0x1028, 0x0cbe, 0xffff, 0x004a },  /* Dell Oasis 13 2-IN-1 MTL-U -> ALC289_FIXUP_DELL_CS35L41_SPI_2 */
  { 0x1028, 0x0cbf, 0xffff, 0x004a },  /* Dell Oasis 13 Low Weight MTU-L -> ALC289_FIXUP_DELL_CS35L41_SPI_2 */
  { 0x1028, 0x0cc0, 0xffff, 0x004b },  /* Dell Oasis 13 -> ALC289_FIXUP_RTK_AMP_DUAL_SPK */
  { 0x1028, 0x0cc1, 0xffff, 0x004a },  /* Dell Oasis 14 MTL-H/U -> ALC289_FIXUP_DELL_CS35L41_SPI_2 */
  { 0x1028, 0x0cc2, 0xffff, 0x004a },  /* Dell Oasis 14 2-in-1 MTL-H/U -> ALC289_FIXUP_DELL_CS35L41_SPI_2 */
  { 0x1028, 0x0cc3, 0xffff, 0x004a },  /* Dell Oasis 14 Low Weight MTL-U -> ALC289_FIXUP_DELL_CS35L41_SPI_2 */
  { 0x1028, 0x0cc4, 0xffff, 0x004a },  /* Dell Oasis 16 MTL-H/U -> ALC289_FIXUP_DELL_CS35L41_SPI_2 */
  { 0x1028, 0x0cc5, 0xffff, 0x004b },  /* Dell Oasis 14 -> ALC289_FIXUP_RTK_AMP_DUAL_SPK */
  { 0x1028, 0x0d56, 0xffff, 0x004f },  /* Dell Pro Tower QCT1255 -> ALC236_FIXUP_DELL_HP_POP_NOISE */
  { 0x1028, 0x0d58, 0xffff, 0x004f },  /* Dell Pro Micro QCM1255 -> ALC236_FIXUP_DELL_HP_POP_NOISE */
  { 0x1028, 0x0d5a, 0xffff, 0x004f },  /* Dell Pro Slim QCS1255 -> ALC236_FIXUP_DELL_HP_POP_NOISE */
  { 0x1028, 0x0e6b, 0xffff, 0x004f },  /* Dell Pro Tower QCT1255 -> ALC236_FIXUP_DELL_HP_POP_NOISE */
  { 0x1028, 0x0e6c, 0xffff, 0x004f },  /* Dell Pro Slim QCS1255 -> ALC236_FIXUP_DELL_HP_POP_NOISE */
  { 0x1028, 0x0e6d, 0xffff, 0x004f },  /* Dell Pro Micro QCM1255 -> ALC236_FIXUP_DELL_HP_POP_NOISE */
  { 0x1028, 0x164a, 0xffff, 0x002e },  /* Dell -> ALC293_FIXUP_DELL1_MIC_NO_PRESENCE */
  { 0x1028, 0x164b, 0xffff, 0x002e },  /* Dell -> ALC293_FIXUP_DELL1_MIC_NO_PRESENCE */
  { 0x103c, 0x1586, 0xffff, 0x7fff },  /* HP -> ALC269_FIXUP_HP_MUTE_LED_MIC2 (nothing to do) */
  { 0x103c, 0x18e6, 0xffff, 0x7fff },  /* HP -> ALC269_FIXUP_HP_GPIO_LED (nothing to do) */
  { 0x103c, 0x2164, 0xffff, 0x7fff },  /* HP Pavilion 15 Notebook PC -> ALC269_FIXUP_LIMIT_INT_MIC_BOOST_MUTE_LED (nothing to do) */
  { 0x103c, 0x218b, 0xffff, 0x7fff },  /* HP -> ALC269_FIXUP_LIMIT_INT_MIC_BOOST_MUTE_LED (nothing to do) */
  { 0x103c, 0x21f9, 0xffff, 0x7fff },  /* HP -> ALC269_FIXUP_HP_MUTE_LED_MIC1 (nothing to do) */
  { 0x103c, 0x2210, 0xffff, 0x7fff },  /* HP -> ALC269_FIXUP_HP_MUTE_LED_MIC1 (nothing to do) */
  { 0x103c, 0x2214, 0xffff, 0x7fff },  /* HP -> ALC269_FIXUP_HP_MUTE_LED_MIC1 (nothing to do) */
  { 0x103c, 0x221b, 0xffff, 0x7fff },  /* HP -> ALC269_FIXUP_HP_GPIO_MIC1_LED (nothing to do) */
  { 0x103c, 0x221c, 0xffff, 0x7fff },  /* HP EliteBook 755 G2 -> ALC280_FIXUP_HP_HEADSET_MIC (nothing to do) */
  { 0x103c, 0x2221, 0xffff, 0x7fff },  /* HP -> ALC269_FIXUP_HP_GPIO_MIC1_LED (nothing to do) */
  { 0x103c, 0x2225, 0xffff, 0x7fff },  /* HP -> ALC269_FIXUP_HP_GPIO_MIC1_LED (nothing to do) */
  { 0x103c, 0x2236, 0xffff, 0x7fff },  /* HP -> ALC269_FIXUP_HP_LINE1_MIC1_LED (nothing to do) */
  { 0x103c, 0x2237, 0xffff, 0x7fff },  /* HP -> ALC269_FIXUP_HP_LINE1_MIC1_LED (nothing to do) */
  { 0x103c, 0x2238, 0xffff, 0x7fff },  /* HP -> ALC269_FIXUP_HP_LINE1_MIC1_LED (nothing to do) */
  { 0x103c, 0x2239, 0xffff, 0x7fff },  /* HP -> ALC269_FIXUP_HP_LINE1_MIC1_LED (nothing to do) */
  { 0x103c, 0x224b, 0xffff, 0x7fff },  /* HP -> ALC269_FIXUP_HP_LINE1_MIC1_LED (nothing to do) */
  { 0x103c, 0x2253, 0xffff, 0x7fff },  /* HP -> ALC269_FIXUP_HP_GPIO_MIC1_LED (nothing to do) */
  { 0x103c, 0x2254, 0xffff, 0x7fff },  /* HP -> ALC269_FIXUP_HP_GPIO_MIC1_LED (nothing to do) */
  { 0x103c, 0x2255, 0xffff, 0x7fff },  /* HP -> ALC269_FIXUP_HP_GPIO_MIC1_LED (nothing to do) */
  { 0x103c, 0x2256, 0xffff, 0x7fff },  /* HP -> ALC269_FIXUP_HP_GPIO_MIC1_LED (nothing to do) */
  { 0x103c, 0x2257, 0xffff, 0x7fff },  /* HP -> ALC269_FIXUP_HP_GPIO_MIC1_LED (nothing to do) */
  { 0x103c, 0x2259, 0xffff, 0x7fff },  /* HP -> ALC269_FIXUP_HP_GPIO_MIC1_LED (nothing to do) */
  { 0x103c, 0x225a, 0xffff, 0x0050 },  /* HP -> ALC269_FIXUP_HP_DOCK_GPIO_MIC1_LED */
  { 0x103c, 0x225f, 0xffff, 0x7fff },  /* HP -> ALC280_FIXUP_HP_GPIO2_MIC_HOTKEY (nothing to do) */
  { 0x103c, 0x2260, 0xffff, 0x7fff },  /* HP -> ALC269_FIXUP_HP_MUTE_LED_MIC1 (nothing to do) */
  { 0x103c, 0x2263, 0xffff, 0x7fff },  /* HP -> ALC269_FIXUP_HP_MUTE_LED_MIC1 (nothing to do) */
  { 0x103c, 0x2264, 0xffff, 0x7fff },  /* HP -> ALC269_FIXUP_HP_MUTE_LED_MIC1 (nothing to do) */
  { 0x103c, 0x2265, 0xffff, 0x7fff },  /* HP -> ALC269_FIXUP_HP_MUTE_LED_MIC1 (nothing to do) */
  { 0x103c, 0x2268, 0xffff, 0x7fff },  /* HP -> ALC269_FIXUP_HP_MUTE_LED_MIC1 (nothing to do) */
  { 0x103c, 0x226a, 0xffff, 0x7fff },  /* HP -> ALC269_FIXUP_HP_MUTE_LED_MIC1 (nothing to do) */
  { 0x103c, 0x226b, 0xffff, 0x7fff },  /* HP -> ALC269_FIXUP_HP_MUTE_LED_MIC1 (nothing to do) */
  { 0x103c, 0x226e, 0xffff, 0x7fff },  /* HP -> ALC269_FIXUP_HP_MUTE_LED_MIC1 (nothing to do) */
  { 0x103c, 0x2271, 0xffff, 0x7fff },  /* HP -> ALC286_FIXUP_HP_GPIO_LED (nothing to do) */
  { 0x103c, 0x2272, 0xffff, 0x7fff },  /* HP -> ALC269_FIXUP_HP_GPIO_MIC1_LED (nothing to do) */
  { 0x103c, 0x2272, 0xffff, 0x0052 },  /* HP -> ALC280_FIXUP_HP_DOCK_PINS */
  { 0x103c, 0x2273, 0xffff, 0x7fff },  /* HP -> ALC269_FIXUP_HP_GPIO_MIC1_LED (nothing to do) */
  { 0x103c, 0x2273, 0xffff, 0x0052 },  /* HP -> ALC280_FIXUP_HP_DOCK_PINS */
  { 0x103c, 0x2278, 0xffff, 0x7fff },  /* HP -> ALC269_FIXUP_HP_GPIO_MIC1_LED (nothing to do) */
  { 0x103c, 0x227f, 0xffff, 0x7fff },  /* HP -> ALC269_FIXUP_HP_MUTE_LED_MIC1 (nothing to do) */
  { 0x103c, 0x2282, 0xffff, 0x7fff },  /* HP -> ALC269_FIXUP_HP_MUTE_LED_MIC1 (nothing to do) */
  { 0x103c, 0x228b, 0xffff, 0x7fff },  /* HP -> ALC269_FIXUP_HP_MUTE_LED_MIC1 (nothing to do) */
  { 0x103c, 0x228e, 0xffff, 0x7fff },  /* HP -> ALC269_FIXUP_HP_MUTE_LED_MIC1 (nothing to do) */
  { 0x103c, 0x229e, 0xffff, 0x7fff },  /* HP -> ALC269_FIXUP_HP_MUTE_LED_MIC1 (nothing to do) */
  { 0x103c, 0x22b2, 0xffff, 0x7fff },  /* HP -> ALC269_FIXUP_HP_MUTE_LED_MIC1 (nothing to do) */
  { 0x103c, 0x22b7, 0xffff, 0x7fff },  /* HP -> ALC269_FIXUP_HP_MUTE_LED_MIC1 (nothing to do) */
  { 0x103c, 0x22bf, 0xffff, 0x7fff },  /* HP -> ALC269_FIXUP_HP_MUTE_LED_MIC1 (nothing to do) */
  { 0x103c, 0x22c4, 0xffff, 0x7fff },  /* HP -> ALC269_FIXUP_HP_MUTE_LED_MIC1 (nothing to do) */
  { 0x103c, 0x22c5, 0xffff, 0x7fff },  /* HP -> ALC269_FIXUP_HP_MUTE_LED_MIC1 (nothing to do) */
  { 0x103c, 0x22c7, 0xffff, 0x7fff },  /* HP -> ALC269_FIXUP_HP_MUTE_LED_MIC1 (nothing to do) */
  { 0x103c, 0x22c8, 0xffff, 0x7fff },  /* HP -> ALC269_FIXUP_HP_MUTE_LED_MIC1 (nothing to do) */
  { 0x103c, 0x22cf, 0xffff, 0x7fff },  /* HP -> ALC269_FIXUP_HP_MUTE_LED_MIC1 (nothing to do) */
  { 0x103c, 0x22db, 0xffff, 0x7fff },  /* HP -> ALC280_FIXUP_HP_9480M (nothing to do) */
  { 0x103c, 0x22dc, 0xffff, 0x7fff },  /* HP -> ALC269_FIXUP_HP_GPIO_MIC1_LED (nothing to do) */
  { 0x103c, 0x22fb, 0xffff, 0x7fff },  /* HP -> ALC269_FIXUP_HP_GPIO_MIC1_LED (nothing to do) */
  { 0x103c, 0x2334, 0xffff, 0x7fff },  /* HP -> ALC269_FIXUP_HP_MUTE_LED_MIC1 (nothing to do) */
  { 0x103c, 0x2335, 0xffff, 0x7fff },  /* HP -> ALC269_FIXUP_HP_MUTE_LED_MIC1 (nothing to do) */
  { 0x103c, 0x2336, 0xffff, 0x7fff },  /* HP -> ALC269_FIXUP_HP_MUTE_LED_MIC1 (nothing to do) */
  { 0x103c, 0x2337, 0xffff, 0x7fff },  /* HP -> ALC269_FIXUP_HP_MUTE_LED_MIC1 (nothing to do) */
  { 0x103c, 0x2b5e, 0xffff, 0x0054 },  /* HP 288 Pro G2 MT -> ALC221_FIXUP_HP_288PRO_MIC_NO_PRESENCE */
  { 0x103c, 0x802e, 0xffff, 0x0055 },  /* HP Z240 SFF -> ALC221_FIXUP_HP_MIC_NO_PRESENCE */
  { 0x103c, 0x802f, 0xffff, 0x0055 },  /* HP Z240 -> ALC221_FIXUP_HP_MIC_NO_PRESENCE */
  { 0x103c, 0x8077, 0xffff, 0x7fff },  /* HP -> ALC256_FIXUP_HP_HEADSET_MIC (nothing to do) */
  { 0x103c, 0x8158, 0xffff, 0x7fff },  /* HP -> ALC256_FIXUP_HP_HEADSET_MIC (nothing to do) */
  { 0x103c, 0x820d, 0xffff, 0x7fff },  /* HP Pavilion 15 -> ALC295_FIXUP_HP_X360 (nothing to do) */
  { 0x103c, 0x8256, 0xffff, 0x0056 },  /* HP -> ALC221_FIXUP_HP_FRONT_MIC */
  { 0x103c, 0x8259, 0xffff, 0x7fff },  /* HP OMEN 15-ax202nf -> ALC269_FIXUP_HP_MUTE_LED_MIC3 (nothing to do) */
  { 0x103c, 0x827e, 0xffff, 0x7fff },  /* HP x360 -> ALC295_FIXUP_HP_X360 (nothing to do) */
  { 0x103c, 0x827f, 0xffff, 0x7fff },  /* HP x360 -> ALC269_FIXUP_HP_MUTE_LED_MIC3 (nothing to do) */
  { 0x103c, 0x82bf, 0xffff, 0x0055 },  /* HP G3 mini -> ALC221_FIXUP_HP_MIC_NO_PRESENCE */
  { 0x103c, 0x82c0, 0xffff, 0x0055 },  /* HP G3 mini premium -> ALC221_FIXUP_HP_MIC_NO_PRESENCE */
  { 0x103c, 0x83b9, 0xffff, 0x7fff },  /* HP Spectre x360 -> ALC269_FIXUP_HP_MUTE_LED_MIC3 (nothing to do) */
  { 0x103c, 0x841c, 0xffff, 0x7fff },  /* HP Pavilion 15-CK0xx -> ALC269_FIXUP_HP_MUTE_LED_MIC3 (nothing to do) */
  { 0x103c, 0x8486, 0xffff, 0x8057 },  /* HP Pavilion x360 [codec SSID] -> ALC295_FIXUP_HP_PAVILION_X360 */
  { 0x103c, 0x8497, 0xffff, 0x7fff },  /* HP Envy x360 -> ALC269_FIXUP_HP_MUTE_LED_MIC3 (nothing to do) */
  { 0x103c, 0x84a6, 0xffff, 0x7fff },  /* HP 250 G7 Notebook PC -> ALC269_FIXUP_HP_LINE1_MIC1_LED (nothing to do) */
  { 0x103c, 0x84ae, 0xffff, 0x7fff },  /* HP 15-db0403ng -> ALC236_FIXUP_HP_MUTE_LED_COEFBIT2 (nothing to do) */
  { 0x103c, 0x84da, 0xffff, 0x005b },  /* HP OMEN dc0019-ur -> ALC295_FIXUP_HP_OMEN */
  { 0x103c, 0x84e7, 0xffff, 0x7fff },  /* HP Pavilion 15 -> ALC269_FIXUP_HP_MUTE_LED_MIC3 (nothing to do) */
  { 0x103c, 0x8519, 0xffff, 0x7fff },  /* HP Spectre x360 15-df0xxx -> ALC285_FIXUP_HP_SPECTRE_X360 (nothing to do) */
  { 0x103c, 0x8536, 0xffff, 0x7fff },  /* HP ProBook 430 G6 -> ALC236_FIXUP_HP_MUTE_LED_MICMUTE_VREF (nothing to do) */
  { 0x103c, 0x8537, 0xffff, 0x7fff },  /* HP ProBook 440 G6 -> ALC236_FIXUP_HP_MUTE_LED_MICMUTE_VREF (nothing to do) */
  { 0x103c, 0x8548, 0xffff, 0x7fff },  /* HP EliteBook x360 830 G6 -> ALC285_FIXUP_HP_GPIO_LED (nothing to do) */
  { 0x103c, 0x854a, 0xffff, 0x7fff },  /* HP EliteBook 830 G6 -> ALC285_FIXUP_HP_GPIO_LED (nothing to do) */
  { 0x103c, 0x854d, 0xffff, 0x7fff },  /* HP EliteBook 840 G6 -> ALC285_FIXUP_HP_GPIO_LED (nothing to do) */
  { 0x103c, 0x856a, 0xffff, 0x7fff },  /* HP Pavilion 15-cs1xxx -> ALC295_FIXUP_HP_PAVILION_MUTE_LED_1B (nothing to do) */
  { 0x103c, 0x85ba, 0xffff, 0x805d },  /* HP Pavilion AiO 24 [codec SSID] -> ALC225_FIXUP_HP_AIO_SPK */
  { 0x103c, 0x85c6, 0xffff, 0x7fff },  /* HP Pavilion x360 Convertible 14-dy1xxx -> ALC295_FIXUP_HP_MUTE_LED_COEFBIT11 (nothing to do) */
  { 0x103c, 0x85de, 0xffff, 0x005e },  /* HP Envy x360 13-ar0xxx -> ALC285_FIXUP_HP_ENVY_X360 */
  { 0x103c, 0x85f0, 0xffff, 0x7fff },  /* HP Laptop 15-dw0xxx -> ALC236_FIXUP_HP_MUTE_LED_COEFBIT2 (nothing to do) */
  { 0x103c, 0x85f1, 0xffff, 0x7fff },  /* HP Laptop 15-dw1xxx -> ALC236_FIXUP_HP_MUTE_LED_MICMUTE_GPIO (nothing to do) */
  { 0x103c, 0x85f3, 0xffff, 0x7fff },  /* HP 250 G8 Notebook PC -> ALC236_FIXUP_HP_MUTE_LED_COEFBIT2 (nothing to do) */
  { 0x103c, 0x8603, 0xffff, 0x7fff },  /* HP Omen 17-cb0xxx -> ALC285_FIXUP_HP_MUTE_LED (nothing to do) */
  { 0x103c, 0x860c, 0xffff, 0x005f },  /* HP ZBook 17 G6 -> ALC285_FIXUP_HP_GPIO_AMP_INIT */
  { 0x103c, 0x860f, 0xffff, 0x005f },  /* HP ZBook 15 G6 -> ALC285_FIXUP_HP_GPIO_AMP_INIT */
  { 0x103c, 0x861f, 0xffff, 0x005f },  /* HP Elite Dragonfly G1 -> ALC285_FIXUP_HP_GPIO_AMP_INIT */
  { 0x103c, 0x864f, 0xffff, 0x7fff },  /* HP Laptop 15-dy0xxx -> ALC236_FIXUP_HP_MUTE_LED_COEFBIT2 (nothing to do) */
  { 0x103c, 0x869d, 0xffff, 0x7fff },  /* HP -> ALC236_FIXUP_HP_MUTE_LED (nothing to do) */
  { 0x103c, 0x86ad, 0xffff, 0x0061 },  /* HP ENVY 13-aq1xxx -> ALC285_FIXUP_HP_ENVY_13AQ */
  { 0x103c, 0x86c1, 0xffff, 0x7fff },  /* HP Laptop 15-da3001TU -> ALC236_FIXUP_HP_MUTE_LED_COEFBIT2 (nothing to do) */
  { 0x103c, 0x86c7, 0xffff, 0x7fff },  /* HP Envy AiO 32 -> ALC274_FIXUP_HP_ENVY_GPIO (nothing to do) */
  { 0x103c, 0x86c8, 0xffff, 0x7fff },  /* HP Laptop 14s-dr1xxx -> ALC236_FIXUP_HP_MUTE_LED_COEFBIT2 (nothing to do) */
  { 0x103c, 0x86e7, 0xffff, 0x7fff },  /* HP Spectre x360 15-eb0xxx -> ALC285_FIXUP_HP_SPECTRE_X360_EB1 (nothing to do) */
  { 0x103c, 0x863e, 0xffff, 0x7fff },  /* HP Spectre x360 15-df1xxx -> ALC285_FIXUP_HP_SPECTRE_X360_DF1 (nothing to do) */
  { 0x103c, 0x86e8, 0xffff, 0x7fff },  /* HP Spectre x360 15-eb0xxx -> ALC285_FIXUP_HP_SPECTRE_X360_EB1 (nothing to do) */
  { 0x103c, 0x86f9, 0xffff, 0x7fff },  /* HP Spectre x360 13-aw0xxx -> ALC285_FIXUP_HP_SPECTRE_X360_MUTE_LED (nothing to do) */
  { 0x103c, 0x8706, 0xffff, 0x7fff },  /* HP Laptop 15s-eq1xxx -> ALC236_FIXUP_HP_MUTE_LED_COEFBIT2 (nothing to do) */
  { 0x103c, 0x8716, 0xffff, 0x005f },  /* HP Elite Dragonfly G2 Notebook PC -> ALC285_FIXUP_HP_GPIO_AMP_INIT */
  { 0x103c, 0x8720, 0xffff, 0x005f },  /* HP EliteBook x360 1040 G8 Notebook PC -> ALC285_FIXUP_HP_GPIO_AMP_INIT */
  { 0x103c, 0x8724, 0xffff, 0x7fff },  /* HP EliteBook 850 G7 -> ALC285_FIXUP_HP_GPIO_LED (nothing to do) */
  { 0x103c, 0x8728, 0xffff, 0x7fff },  /* HP EliteBook 840 G7 -> ALC285_FIXUP_HP_GPIO_LED (nothing to do) */
  { 0x103c, 0x8729, 0xffff, 0x7fff },  /* HP -> ALC285_FIXUP_HP_GPIO_LED (nothing to do) */
  { 0x103c, 0x8730, 0xffff, 0x7fff },  /* HP ProBook 445 G7 -> ALC236_FIXUP_HP_MUTE_LED_MICMUTE_VREF (nothing to do) */
  { 0x103c, 0x8735, 0xffff, 0x7fff },  /* HP ProBook 435 G7 -> ALC236_FIXUP_HP_MUTE_LED_MICMUTE_VREF (nothing to do) */
  { 0x103c, 0x8736, 0xffff, 0x005f },  /* HP -> ALC285_FIXUP_HP_GPIO_AMP_INIT */
  { 0x103c, 0x8756, 0xffff, 0x7fff },  /* HP ENVY Laptop 13-ba0xxx -> ALC245_FIXUP_HP_X360_MUTE_LEDS (nothing to do) */
  { 0x103c, 0x8760, 0xffff, 0x7fff },  /* HP EliteBook 8{4,5}5 G7 -> ALC285_FIXUP_HP_BEEP_MICMUTE_LED (nothing to do) */
  { 0x103c, 0x876e, 0xffff, 0x7fff },  /* HP ENVY x360 Convertible 13-ay0xxx -> ALC245_FIXUP_HP_X360_MUTE_LEDS (nothing to do) */
  { 0x103c, 0x877a, 0xffff, 0x7fff },  /* HP -> ALC285_FIXUP_HP_MUTE_LED (nothing to do) */
  { 0x103c, 0x877d, 0xffff, 0x7fff },  /* HP -> ALC236_FIXUP_HP_MUTE_LED (nothing to do) */
  { 0x103c, 0x8780, 0xffff, 0x005f },  /* HP ZBook Fury 17 G7 Mobile Workstation -> ALC285_FIXUP_HP_GPIO_AMP_INIT */
  { 0x103c, 0x8783, 0xffff, 0x005f },  /* HP ZBook Fury 15 G7 Mobile Workstation -> ALC285_FIXUP_HP_GPIO_AMP_INIT */
  { 0x103c, 0x8786, 0xffff, 0x7fff },  /* HP OMEN 15 -> ALC285_FIXUP_HP_MUTE_LED (nothing to do) */
  { 0x103c, 0x8787, 0xffff, 0x7fff },  /* HP OMEN 15 -> ALC285_FIXUP_HP_MUTE_LED (nothing to do) */
  { 0x103c, 0x8788, 0xffff, 0x7fff },  /* HP OMEN 15 -> ALC285_FIXUP_HP_MUTE_LED (nothing to do) */
  { 0x103c, 0x87b2, 0xffff, 0x7fff },  /* HP Pavilion Gaming Laptop 15-ec1xxx -> ALC285_FIXUP_HP_MUTE_LED (nothing to do) */
  { 0x103c, 0x87b7, 0xffff, 0x7fff },  /* HP Laptop 14-fq0xxx -> ALC236_FIXUP_HP_MUTE_LED_COEFBIT2 (nothing to do) */
  { 0x103c, 0x87c8, 0xffff, 0x7fff },  /* HP -> ALC287_FIXUP_HP_GPIO_LED (nothing to do) */
  { 0x103c, 0x87cb, 0xffff, 0x7fff },  /* HP Pavilion 15-eg0xxx -> ALC287_FIXUP_HP_GPIO_LED (nothing to do) */
  { 0x103c, 0x87cc, 0xffff, 0x7fff },  /* HP Pavilion 15-eg0xxx -> ALC287_FIXUP_HP_GPIO_LED (nothing to do) */
  { 0x103c, 0x87d3, 0xffff, 0x7fff },  /* HP Laptop 15-gw0xxx -> ALC236_FIXUP_HP_MUTE_LED_COEFBIT2 (nothing to do) */
  { 0x103c, 0x87df, 0xffff, 0x7fff },  /* HP ProBook 430 G8 Notebook PC -> ALC236_FIXUP_HP_GPIO_LED (nothing to do) */
  { 0x103c, 0x87e5, 0xffff, 0x7fff },  /* HP ProBook 440 G8 Notebook PC -> ALC236_FIXUP_HP_GPIO_LED (nothing to do) */
  { 0x103c, 0x87e7, 0xffff, 0x7fff },  /* HP ProBook 450 G8 Notebook PC -> ALC236_FIXUP_HP_GPIO_LED (nothing to do) */
  { 0x103c, 0x87f1, 0xffff, 0x7fff },  /* HP ProBook 630 G8 Notebook PC -> ALC236_FIXUP_HP_GPIO_LED (nothing to do) */
  { 0x103c, 0x87f2, 0xffff, 0x7fff },  /* HP ProBook 640 G8 Notebook PC -> ALC236_FIXUP_HP_GPIO_LED (nothing to do) */
  { 0x103c, 0x87f4, 0xffff, 0x7fff },  /* HP -> ALC287_FIXUP_HP_GPIO_LED (nothing to do) */
  { 0x103c, 0x87f5, 0xffff, 0x7fff },  /* HP -> ALC287_FIXUP_HP_GPIO_LED (nothing to do) */
  { 0x103c, 0x87f6, 0xffff, 0x0062 },  /* HP Spectre x360 14 -> ALC245_FIXUP_HP_X360_AMP */
  { 0x103c, 0x87f7, 0xffff, 0x0062 },  /* HP Spectre x360 14 -> ALC245_FIXUP_HP_X360_AMP */
  { 0x103c, 0x87fd, 0xffff, 0x7fff },  /* HP Laptop 14-dq2xxx -> ALC236_FIXUP_HP_MUTE_LED_COEFBIT2 (nothing to do) */
  { 0x103c, 0x87fe, 0xffff, 0x7fff },  /* HP Laptop 15s-fq2xxx -> ALC236_FIXUP_HP_MUTE_LED_COEFBIT2 (nothing to do) */
  { 0x103c, 0x8805, 0xffff, 0x7fff },  /* HP ProBook 650 G8 Notebook PC -> ALC236_FIXUP_HP_GPIO_LED (nothing to do) */
  { 0x103c, 0x880d, 0xffff, 0x7fff },  /* HP EliteBook 830 G8 Notebook PC -> ALC285_FIXUP_HP_GPIO_LED (nothing to do) */
  { 0x103c, 0x8811, 0xffff, 0x7fff },  /* HP Spectre x360 15-eb1xxx -> ALC285_FIXUP_HP_SPECTRE_X360_EB1 (nothing to do) */
  { 0x103c, 0x8812, 0xffff, 0x7fff },  /* HP Spectre x360 15-eb1xxx -> ALC285_FIXUP_HP_SPECTRE_X360_EB1 (nothing to do) */
  { 0x103c, 0x881d, 0xffff, 0x7fff },  /* HP 250 G8 Notebook PC -> ALC236_FIXUP_HP_MUTE_LED_COEFBIT2 (nothing to do) */
  { 0x103c, 0x881e, 0xffff, 0x7fff },  /* HP Laptop 15s-du3xxx -> ALC236_FIXUP_HP_MUTE_LED_COEFBIT2 (nothing to do) */
  { 0x103c, 0x8846, 0xffff, 0x7fff },  /* HP EliteBook 850 G8 Notebook PC -> ALC285_FIXUP_HP_GPIO_LED (nothing to do) */
  { 0x103c, 0x8847, 0xffff, 0x7fff },  /* HP EliteBook x360 830 G8 Notebook PC -> ALC285_FIXUP_HP_GPIO_LED (nothing to do) */
  { 0x103c, 0x884b, 0xffff, 0x7fff },  /* HP EliteBook 840 Aero G8 Notebook PC -> ALC285_FIXUP_HP_GPIO_LED (nothing to do) */
  { 0x103c, 0x884c, 0xffff, 0x7fff },  /* HP EliteBook 840 G8 Notebook PC -> ALC285_FIXUP_HP_GPIO_LED (nothing to do) */
  { 0x103c, 0x885b, 0xffff, 0x8062 },  /* HP Spectre x360 14-ea [codec SSID] -> ALC245_FIXUP_HP_X360_AMP */
  { 0x103c, 0x8862, 0xffff, 0x7fff },  /* HP ProBook 445 G8 Notebook PC -> ALC236_FIXUP_HP_LIMIT_INT_MIC_BOOST (nothing to do) */
  { 0x103c, 0x8863, 0xffff, 0x7fff },  /* HP ProBook 445 G8 Notebook PC -> ALC236_FIXUP_HP_LIMIT_INT_MIC_BOOST (nothing to do) */
  { 0x103c, 0x886d, 0xffff, 0x005f },  /* HP ZBook Fury 17.3 Inch G8 Mobile Workstation PC -> ALC285_FIXUP_HP_GPIO_AMP_INIT */
  { 0x103c, 0x8870, 0xffff, 0x005f },  /* HP ZBook Fury 15.6 Inch G8 Mobile Workstation PC -> ALC285_FIXUP_HP_GPIO_AMP_INIT */
  { 0x103c, 0x8873, 0xffff, 0x005f },  /* HP ZBook Studio 15.6 Inch G8 Mobile Workstation PC -> ALC285_FIXUP_HP_GPIO_AMP_INIT */
  { 0x103c, 0x887a, 0xffff, 0x7fff },  /* HP Laptop 15s-eq2xxx -> ALC236_FIXUP_HP_MUTE_LED_COEFBIT2 (nothing to do) */
  { 0x103c, 0x887c, 0xffff, 0x7fff },  /* HP Laptop 14s-fq1xxx -> ALC236_FIXUP_HP_MUTE_LED_COEFBIT2 (nothing to do) */
  { 0x103c, 0x888a, 0xffff, 0x7fff },  /* HP ENVY x360 Convertible 15-eu0xxx -> ALC245_FIXUP_HP_X360_MUTE_LEDS (nothing to do) */
  { 0x103c, 0x888d, 0xffff, 0x7fff },  /* HP ZBook Power 15.6 inch G8 Mobile Workstation PC -> ALC236_FIXUP_HP_GPIO_LED (nothing to do) */
  { 0x103c, 0x8890, 0xffff, 0x005f },  /* HP Elite Dragonfly Max G2 Notebook PC -> ALC285_FIXUP_HP_GPIO_AMP_INIT */
  { 0x103c, 0x8895, 0xffff, 0x0064 },  /* HP EliteBook 855 G8 Notebook PC -> ALC285_FIXUP_HP_SPEAKERS_MICMUTE_LED */
  { 0x103c, 0x8896, 0xffff, 0x7fff },  /* HP EliteBook 855 G8 Notebook PC -> ALC285_FIXUP_HP_MUTE_LED (nothing to do) */
  { 0x103c, 0x8898, 0xffff, 0x7fff },  /* HP EliteBook 845 G8 Notebook PC -> ALC285_FIXUP_HP_LIMIT_INT_MIC_BOOST (nothing to do) */
  { 0x103c, 0x88b3, 0xffff, 0x7fff },  /* HP ENVY x360 Convertible 15-es0xxx -> ALC245_FIXUP_HP_ENVY_X360_MUTE_LED (nothing to do) */
  { 0x103c, 0x88d0, 0xffff, 0x7fff },  /* HP Pavilion 15-eh1xxx (mainboard 88D0) -> ALC287_FIXUP_HP_GPIO_LED (nothing to do) */
  { 0x103c, 0x88d1, 0xffff, 0x7fff },  /* HP Pavilion 15-eh1xxx (mainboard 88D1) -> ALC245_FIXUP_HP_MUTE_LED_V1_COEFBIT (nothing to do) */
  { 0x103c, 0x88dd, 0xffff, 0x7fff },  /* HP Pavilion 15z-ec200 -> ALC285_FIXUP_HP_MUTE_LED (nothing to do) */
  { 0x103c, 0x88eb, 0xffff, 0x7fff },  /* HP Victus 16-e0xxx -> ALC245_FIXUP_HP_MUTE_LED_V2_COEFBIT (nothing to do) */
  { 0x103c, 0x88ed, 0xffff, 0x7fff },  /* HP Victus 16-e0xxx (MB 88ED) -> ALC245_FIXUP_HP_MUTE_LED_V2_COEFBIT (nothing to do) */
  { 0x103c, 0x88ee, 0xffff, 0x7fff },  /* HP Victus 16-e0xxx (MB 88EE) -> ALC245_FIXUP_HP_MUTE_LED_COEFBIT (nothing to do) */
  { 0x103c, 0x8902, 0xffff, 0x7fff },  /* HP OMEN 16 -> ALC285_FIXUP_HP_MUTE_LED (nothing to do) */
  { 0x103c, 0x890e, 0xffff, 0x7fff },  /* HP 255 G8 Notebook PC -> ALC236_FIXUP_HP_MUTE_LED_COEFBIT2 (nothing to do) */
  { 0x103c, 0x8919, 0xffff, 0x7fff },  /* HP Pavilion Aero Laptop 13-be0xxx -> ALC287_FIXUP_HP_GPIO_LED (nothing to do) */
  { 0x103c, 0x896d, 0xffff, 0x7fff },  /* HP ZBook Firefly 16 G9 -> ALC245_FIXUP_CS35L41_SPI_2_HP_GPIO_LED (nothing to do) */
  { 0x103c, 0x896e, 0xffff, 0x7fff },  /* HP EliteBook x360 830 G9 -> ALC245_FIXUP_CS35L41_SPI_2_HP_GPIO_LED (nothing to do) */
  { 0x103c, 0x8971, 0xffff, 0x7fff },  /* HP EliteBook 830 G9 -> ALC245_FIXUP_CS35L41_SPI_2_HP_GPIO_LED (nothing to do) */
  { 0x103c, 0x8972, 0xffff, 0x7fff },  /* HP EliteBook 840 G9 -> ALC245_FIXUP_CS35L41_SPI_2_HP_GPIO_LED (nothing to do) */
  { 0x103c, 0x8973, 0xffff, 0x7fff },  /* HP EliteBook 860 G9 -> ALC245_FIXUP_CS35L41_SPI_2_HP_GPIO_LED (nothing to do) */
  { 0x103c, 0x8974, 0xffff, 0x7fff },  /* HP EliteBook 840 Aero G9 -> ALC245_FIXUP_CS35L41_SPI_2_HP_GPIO_LED (nothing to do) */
  { 0x103c, 0x8975, 0xffff, 0x7fff },  /* HP EliteBook x360 840 Aero G9 -> ALC245_FIXUP_CS35L41_SPI_2_HP_GPIO_LED (nothing to do) */
  { 0x103c, 0x897d, 0xffff, 0x7fff },  /* HP mt440 Mobile Thin Client U74 -> ALC236_FIXUP_HP_GPIO_LED (nothing to do) */
  { 0x103c, 0x8981, 0xffff, 0x7fff },  /* HP Elite Dragonfly G3 -> ALC245_FIXUP_CS35L41_SPI_4 (nothing to do) */
  { 0x103c, 0x8987, 0xffff, 0x7fff },  /* HP Pavilion 14-dv1001TU -> ALC287_FIXUP_HP_GPIO_LED (nothing to do) */
  { 0x103c, 0x898a, 0xffff, 0x7fff },  /* HP Pavilion 15-eg100 -> ALC287_FIXUP_HP_GPIO_LED (nothing to do) */
  { 0x103c, 0x898e, 0xffff, 0x7fff },  /* HP EliteBook 835 G9 -> ALC287_FIXUP_CS35L41_I2C_2 (nothing to do) */
  { 0x103c, 0x898f, 0xffff, 0x7fff },  /* HP EliteBook 835 G9 -> ALC287_FIXUP_CS35L41_I2C_2 (nothing to do) */
  { 0x103c, 0x8991, 0xffff, 0x7fff },  /* HP EliteBook 845 G9 -> ALC287_FIXUP_CS35L41_I2C_2_HP_GPIO_LED (nothing to do) */
  { 0x103c, 0x8992, 0xffff, 0x7fff },  /* HP EliteBook 845 G9 -> ALC287_FIXUP_CS35L41_I2C_2 (nothing to do) */
  { 0x103c, 0x8994, 0xffff, 0x7fff },  /* HP EliteBook 855 G9 -> ALC287_FIXUP_CS35L41_I2C_2_HP_GPIO_LED (nothing to do) */
  { 0x103c, 0x8995, 0xffff, 0x7fff },  /* HP EliteBook 855 G9 -> ALC287_FIXUP_CS35L41_I2C_2 (nothing to do) */
  { 0x103c, 0x89a0, 0xffff, 0x7fff },  /* HP Laptop 15-dw4xxx -> ALC236_FIXUP_HP_MUTE_LED_COEFBIT2 (nothing to do) */
  { 0x103c, 0x89a4, 0xffff, 0x7fff },  /* HP ProBook 440 G9 -> ALC236_FIXUP_HP_GPIO_LED (nothing to do) */
  { 0x103c, 0x89a6, 0xffff, 0x7fff },  /* HP ProBook 450 G9 -> ALC236_FIXUP_HP_GPIO_LED (nothing to do) */
  { 0x103c, 0x89aa, 0xffff, 0x7fff },  /* HP EliteBook 630 G9 -> ALC236_FIXUP_HP_GPIO_LED (nothing to do) */
  { 0x103c, 0x89ac, 0xffff, 0x7fff },  /* HP EliteBook 640 G9 -> ALC236_FIXUP_HP_GPIO_LED (nothing to do) */
  { 0x103c, 0x89ae, 0xffff, 0x7fff },  /* HP EliteBook 650 G9 -> ALC236_FIXUP_HP_GPIO_LED (nothing to do) */
  { 0x103c, 0x89c0, 0xffff, 0x7fff },  /* HP ZBook Power 15.6 G9 -> ALC245_FIXUP_CS35L41_SPI_2_HP_GPIO_LED (nothing to do) */
  { 0x103c, 0x89c3, 0xffff, 0x7fff },  /* Zbook Studio G9 -> ALC245_FIXUP_CS35L41_SPI_4_HP_GPIO_LED (nothing to do) */
  { 0x103c, 0x89c6, 0xffff, 0x7fff },  /* Zbook Fury 17 G9 -> ALC245_FIXUP_CS35L41_SPI_2_HP_GPIO_LED (nothing to do) */
  { 0x103c, 0x89ca, 0xffff, 0x7fff },  /* HP -> ALC236_FIXUP_HP_MUTE_LED_MICMUTE_VREF (nothing to do) */
  { 0x103c, 0x89d3, 0xffff, 0x7fff },  /* HP EliteBook 645 G9 (MB 89D2) -> ALC236_FIXUP_HP_MUTE_LED_MICMUTE_VREF (nothing to do) */
  { 0x103c, 0x89da, 0xffff, 0x7fff },  /* HP Spectre x360 14t-ea100 -> ALC245_FIXUP_HP_SPECTRE_X360_EU0XXX (nothing to do) */
  { 0x103c, 0x89e7, 0xffff, 0x7fff },  /* HP Elite x2 G9 -> ALC245_FIXUP_CS35L41_SPI_2_HP_GPIO_LED (nothing to do) */
  { 0x103c, 0x8a05, 0xffff, 0x7fff },  /* HP Dragonfly Folio G3 2-in-1 -> ALC245_FIXUP_CS35L41_SPI_4_HP_GPIO_LED (nothing to do) */
  { 0x103c, 0x8a06, 0xffff, 0x7fff },  /* HP Dragonfly Folio G3 2-in-1 -> ALC245_FIXUP_CS35L41_SPI_4_HP_GPIO_LED (nothing to do) */
  { 0x103c, 0x8a0e, 0xffff, 0x7fff },  /* HP Pavilion Laptop 15-eh2xxx -> ALC287_FIXUP_HP_GPIO_LED (nothing to do) */
  { 0x103c, 0x8a0f, 0xffff, 0x7fff },  /* HP Pavilion 14-ec1xxx -> ALC287_FIXUP_HP_GPIO_LED (nothing to do) */
  { 0x103c, 0x8a1b, 0xffff, 0x7fff },  /* HP 255 15.6 inch G9 Notebook PC -> ALC236_FIXUP_HP_MUTE_LED_COEFBIT2 (nothing to do) */
  { 0x103c, 0x8a1f, 0xffff, 0x7fff },  /* HP Laptop 14s-dr5xxx -> ALC236_FIXUP_HP_MUTE_LED_COEFBIT2 (nothing to do) */
  { 0x103c, 0x8a20, 0xffff, 0x7fff },  /* HP Laptop 15s-fq5xxx -> ALC236_FIXUP_HP_MUTE_LED_COEFBIT2 (nothing to do) */
  { 0x103c, 0x8a25, 0xffff, 0x7fff },  /* HP Victus 16-d1xxx (MB 8A25) -> ALC245_FIXUP_HP_MUTE_LED_COEFBIT (nothing to do) */
  { 0x103c, 0x8a26, 0xffff, 0x7fff },  /* HP Victus 16-d1xxx (MB 8A26) -> ALC245_FIXUP_HP_MUTE_LED_COEFBIT (nothing to do) */
  { 0x103c, 0x8a28, 0xffff, 0x7fff },  /* HP Envy 13 -> ALC287_FIXUP_CS35L41_I2C_2 (nothing to do) */
  { 0x103c, 0x8a29, 0xffff, 0x7fff },  /* HP Envy 15 -> ALC287_FIXUP_CS35L41_I2C_2 (nothing to do) */
  { 0x103c, 0x8a2a, 0xffff, 0x7fff },  /* HP Envy 15 -> ALC287_FIXUP_CS35L41_I2C_2 (nothing to do) */
  { 0x103c, 0x8a2b, 0xffff, 0x7fff },  /* HP Envy 15 -> ALC287_FIXUP_CS35L41_I2C_2 (nothing to do) */
  { 0x103c, 0x8a2c, 0xffff, 0x7fff },  /* HP Envy 16 -> ALC287_FIXUP_CS35L41_I2C_2 (nothing to do) */
  { 0x103c, 0x8a2d, 0xffff, 0x7fff },  /* HP Envy 16 -> ALC287_FIXUP_CS35L41_I2C_2 (nothing to do) */
  { 0x103c, 0x8a2e, 0xffff, 0x7fff },  /* HP Envy 16 -> ALC287_FIXUP_CS35L41_I2C_2 (nothing to do) */
  { 0x103c, 0x8a30, 0xffff, 0x7fff },  /* HP Envy 17 -> ALC287_FIXUP_CS35L41_I2C_2 (nothing to do) */
  { 0x103c, 0x8a31, 0xffff, 0x7fff },  /* HP ENVY x360 2-in-1 Laptop 15-ey0xxx -> ALC287_FIXUP_CS35L41_I2C_2_HP_MUTE_LEDS (nothing to do) */
  { 0x103c, 0x8a34, 0xffff, 0x7fff },  /* HP Pavilion x360 2-in-1 Laptop 14-ek0xxx -> ALC245_FIXUP_HP_MUTE_LED_COEFBIT (nothing to do) */
  { 0x103c, 0x8a36, 0xffff, 0x7fff },  /* HP Pavilion Plus 14-eh0xxx -> ALC245_FIXUP_HP_MUTE_LED_COEFBIT (nothing to do) */
  { 0x103c, 0x8a3d, 0xffff, 0x7fff },  /* HP Victus 15-fb0xxx (MB 8A3D) -> ALC245_FIXUP_HP_MUTE_LED_V2_COEFBIT (nothing to do) */
  { 0x103c, 0x8a4f, 0xffff, 0x7fff },  /* HP Victus 15-fa0xxx (MB 8A4F) -> ALC245_FIXUP_HP_MUTE_LED_COEFBIT (nothing to do) */
  { 0x103c, 0x8a50, 0xffff, 0x7fff },  /* HP Victus 15-fa0xxx (MB 8A50) -> ALC245_FIXUP_HP_MUTE_LED_COEFBIT (nothing to do) */
  { 0x103c, 0x8a6b, 0xffff, 0x0066 },  /* HP Pavilion All-in-One Desktop 27-ca1xxx -> ALC274_FIXUP_HP_VERBS */
  { 0x103c, 0x8a6e, 0xffff, 0x7fff },  /* HP EDNA 360 -> ALC287_FIXUP_CS35L41_I2C_4 (nothing to do) */
  { 0x103c, 0x8a74, 0xffff, 0x7fff },  /* HP ProBook 440 G8 Notebook PC -> ALC236_FIXUP_HP_GPIO_LED (nothing to do) */
  { 0x103c, 0x8a75, 0xffff, 0x7fff },  /* HP ProBook 450 G8 Notebook PC -> ALC236_FIXUP_HP_GPIO_LED (nothing to do) */
  { 0x103c, 0x8a76, 0xffff, 0x7fff },  /* HP ProBook 440 G8 Notebook PC -> ALC236_FIXUP_HP_GPIO_LED (nothing to do) */
  { 0x103c, 0x8a77, 0xffff, 0x7fff },  /* HP ProBook 450 G8 Notebook PC -> ALC236_FIXUP_HP_GPIO_LED (nothing to do) */
  { 0x103c, 0x8a78, 0xffff, 0x7fff },  /* HP Dev One -> ALC285_FIXUP_HP_LIMIT_INT_MIC_BOOST (nothing to do) */
  { 0x103c, 0x8aa0, 0xffff, 0x7fff },  /* HP ProBook 440 G9 (MB 8A9E) -> ALC236_FIXUP_HP_GPIO_LED (nothing to do) */
  { 0x103c, 0x8aa3, 0xffff, 0x7fff },  /* HP ProBook 450 G9 (MB 8AA1) -> ALC236_FIXUP_HP_GPIO_LED (nothing to do) */
  { 0x103c, 0x8aa8, 0xffff, 0x7fff },  /* HP EliteBook 640 G9 (MB 8AA6) -> ALC236_FIXUP_HP_GPIO_LED (nothing to do) */
  { 0x103c, 0x8aab, 0xffff, 0x7fff },  /* HP EliteBook 650 G9 (MB 8AA9) -> ALC236_FIXUP_HP_GPIO_LED (nothing to do) */
  { 0x103c, 0x8ab8, 0xffff, 0x7fff },  /* HP EliteBook 830 G8 Notebook PC (MB 8AB8) -> ALC245_FIXUP_CS35L41_SPI_2_HP_GPIO_LED (nothing to do) */
  { 0x103c, 0x8ab9, 0xffff, 0x7fff },  /* HP EliteBook 840 G8 (MB 8AB8) -> ALC285_FIXUP_HP_GPIO_LED (nothing to do) */
  { 0x103c, 0x8abb, 0xffff, 0x7fff },  /* HP ZBook Firefly 14 G9 -> ALC245_FIXUP_CS35L41_SPI_2_HP_GPIO_LED (nothing to do) */
  { 0x103c, 0x8ad1, 0xffff, 0x7fff },  /* HP EliteBook 840 14 inch G9 Notebook PC -> ALC245_FIXUP_CS35L41_SPI_2_HP_GPIO_LED (nothing to do) */
  { 0x103c, 0x8ad2, 0xffff, 0x7fff },  /* HP EliteBook 860 16 inch G9 Notebook PC -> ALC245_FIXUP_CS35L41_SPI_2_HP_GPIO_LED (nothing to do) */
  { 0x103c, 0x8ad8, 0xffff, 0x7fff },  /* HP 800 G9 -> ALC245_FIXUP_CS35L41_SPI_2_HP_GPIO_LED (nothing to do) */
  { 0x103c, 0x8b0f, 0xffff, 0x7fff },  /* HP Elite mt645 G7 Mobile Thin Client U81 -> ALC236_FIXUP_HP_MUTE_LED_MICMUTE_VREF (nothing to do) */
  { 0x103c, 0x8b2f, 0xffff, 0x7fff },  /* HP 255 15.6 inch G10 Notebook PC -> ALC236_FIXUP_HP_MUTE_LED_COEFBIT2 (nothing to do) */
  { 0x103c, 0x8b34, 0xffff, 0x7fff },  /* HP 250 15.6 inch G10 Notebook PC -> ALC236_FIXUP_HP_MUTE_LED_COEFBIT2 (nothing to do) */
  { 0x103c, 0x8b3a, 0xffff, 0x7fff },  /* HP Envy 15 -> ALC287_FIXUP_CS35L41_I2C_2 (nothing to do) */
  { 0x103c, 0x8b3f, 0xffff, 0x7fff },  /* HP mt440 Mobile Thin Client U91 -> ALC236_FIXUP_HP_GPIO_LED (nothing to do) */
  { 0x103c, 0x8b42, 0xffff, 0x7fff },  /* HP -> ALC245_FIXUP_CS35L41_SPI_2_HP_GPIO_LED (nothing to do) */
  { 0x103c, 0x8b43, 0xffff, 0x7fff },  /* HP -> ALC245_FIXUP_CS35L41_SPI_2_HP_GPIO_LED (nothing to do) */
  { 0x103c, 0x8b44, 0xffff, 0x7fff },  /* HP -> ALC245_FIXUP_CS35L41_SPI_2_HP_GPIO_LED (nothing to do) */
  { 0x103c, 0x8b45, 0xffff, 0x7fff },  /* HP -> ALC245_FIXUP_CS35L41_SPI_2_HP_GPIO_LED (nothing to do) */
  { 0x103c, 0x8b46, 0xffff, 0x7fff },  /* HP -> ALC245_FIXUP_CS35L41_SPI_2_HP_GPIO_LED (nothing to do) */
  { 0x103c, 0x8b47, 0xffff, 0x7fff },  /* HP -> ALC245_FIXUP_CS35L41_SPI_2_HP_GPIO_LED (nothing to do) */
  { 0x103c, 0x8b59, 0xffff, 0x7fff },  /* HP Elite mt645 G7 Mobile Thin Client U89 -> ALC236_FIXUP_HP_MUTE_LED_MICMUTE_VREF (nothing to do) */
  { 0x103c, 0x8b5d, 0xffff, 0x7fff },  /* HP -> ALC236_FIXUP_HP_MUTE_LED_MICMUTE_VREF (nothing to do) */
  { 0x103c, 0x8b5e, 0xffff, 0x7fff },  /* HP -> ALC236_FIXUP_HP_MUTE_LED_MICMUTE_VREF (nothing to do) */
  { 0x103c, 0x8b5f, 0xffff, 0x7fff },  /* HP -> ALC236_FIXUP_HP_MUTE_LED_MICMUTE_VREF (nothing to do) */
  { 0x103c, 0x8b63, 0xffff, 0x7fff },  /* HP Elite Dragonfly 13.5 inch G4 -> ALC245_FIXUP_CS35L41_SPI_4_HP_GPIO_LED (nothing to do) */
  { 0x103c, 0x8b65, 0xffff, 0x7fff },  /* HP ProBook 455 15.6 inch G10 Notebook PC -> ALC236_FIXUP_HP_MUTE_LED_MICMUTE_VREF (nothing to do) */
  { 0x103c, 0x8b66, 0xffff, 0x7fff },  /* HP -> ALC236_FIXUP_HP_MUTE_LED_MICMUTE_VREF (nothing to do) */
  { 0x103c, 0x8b70, 0xffff, 0x7fff },  /* HP EliteBook 835 G10 -> ALC287_FIXUP_CS35L41_I2C_2_HP_GPIO_LED (nothing to do) */
  { 0x103c, 0x8b72, 0xffff, 0x7fff },  /* HP EliteBook 845 G10 -> ALC287_FIXUP_CS35L41_I2C_2_HP_GPIO_LED (nothing to do) */
  { 0x103c, 0x8b74, 0xffff, 0x7fff },  /* HP EliteBook 845W G10 -> ALC287_FIXUP_CS35L41_I2C_2_HP_GPIO_LED (nothing to do) */
  { 0x103c, 0x8b77, 0xffff, 0x7fff },  /* HP ElieBook 865 G10 -> ALC287_FIXUP_CS35L41_I2C_2 (nothing to do) */
  { 0x103c, 0x8b7a, 0xffff, 0x7fff },  /* HP -> ALC236_FIXUP_HP_GPIO_LED (nothing to do) */
  { 0x103c, 0x8b7d, 0xffff, 0x7fff },  /* HP -> ALC236_FIXUP_HP_GPIO_LED (nothing to do) */
  { 0x103c, 0x8b87, 0xffff, 0x7fff },  /* HP -> ALC236_FIXUP_HP_GPIO_LED (nothing to do) */
  { 0x103c, 0x8b8a, 0xffff, 0x7fff },  /* HP -> ALC236_FIXUP_HP_GPIO_LED (nothing to do) */
  { 0x103c, 0x8b8b, 0xffff, 0x7fff },  /* HP -> ALC236_FIXUP_HP_GPIO_LED (nothing to do) */
  { 0x103c, 0x8b8d, 0xffff, 0x7fff },  /* HP -> ALC236_FIXUP_HP_GPIO_LED (nothing to do) */
  { 0x103c, 0x8b8f, 0xffff, 0x7fff },  /* HP -> ALC245_FIXUP_CS35L41_SPI_4_HP_GPIO_LED (nothing to do) */
  { 0x103c, 0x8b92, 0xffff, 0x7fff },  /* HP -> ALC245_FIXUP_CS35L41_SPI_2_HP_GPIO_LED (nothing to do) */
  { 0x103c, 0x8b96, 0xffff, 0x7fff },  /* HP -> ALC236_FIXUP_HP_MUTE_LED_MICMUTE_VREF (nothing to do) */
  { 0x103c, 0x8b97, 0xffff, 0x7fff },  /* HP -> ALC236_FIXUP_HP_MUTE_LED_MICMUTE_VREF (nothing to do) */
  { 0x103c, 0x8ba9, 0xffff, 0x7fff },  /* HP Omen 16-wd0xxx -> ALC245_FIXUP_HP_MUTE_LED_V1_COEFBIT (nothing to do) */
  { 0x103c, 0x8bb1, 0xffff, 0x7fff },  /* HP Victus 15-fa1xxx (MB 8BB1) -> ALC245_FIXUP_HP_MUTE_LED_COEFBIT (nothing to do) */
  { 0x103c, 0x8bb3, 0xffff, 0x7fff },  /* HP Slim OMEN -> ALC287_FIXUP_CS35L41_I2C_2 (nothing to do) */
  { 0x103c, 0x8bb4, 0xffff, 0x7fff },  /* HP Slim OMEN -> ALC287_FIXUP_CS35L41_I2C_2 (nothing to do) */
  { 0x103c, 0x8bb6, 0xffff, 0x7fff },  /* HP Laptop 15-fd0039nt -> ALC236_FIXUP_HP_15_FD0XXX (nothing to do) */
  { 0x103c, 0x8bbe, 0xffff, 0x7fff },  /* HP Victus 16-r0xxx (MB 8BBE) -> ALC245_FIXUP_HP_MUTE_LED_COEFBIT (nothing to do) */
  { 0x103c, 0x8bc7, 0xffff, 0x7fff },  /* HP Pavilion 15-eh3174nw -> ALC287_FIXUP_HP_GPIO_LED (nothing to do) */
  { 0x103c, 0x8bc8, 0xffff, 0x7fff },  /* HP Victus 15-fa1xxx -> ALC245_FIXUP_HP_MUTE_LED_COEFBIT (nothing to do) */
  { 0x103c, 0x8bcd, 0xffff, 0x7fff },  /* HP Omen 16-xd0xxx -> ALC245_FIXUP_HP_MUTE_LED_V1_COEFBIT (nothing to do) */
  { 0x103c, 0x8bd4, 0xffff, 0x7fff },  /* HP Victus 16-s0xxx (MB 8BD4) -> ALC245_FIXUP_HP_MUTE_LED_COEFBIT (nothing to do) */
  { 0x103c, 0x8bd6, 0xffff, 0x7fff },  /* HP Pavilion Aero Laptop 13z-be200 -> ALC287_FIXUP_HP_GPIO_LED (nothing to do) */
  { 0x103c, 0x8bdd, 0xffff, 0x7fff },  /* HP Envy 17 -> ALC287_FIXUP_CS35L41_I2C_2 (nothing to do) */
  { 0x103c, 0x8bde, 0xffff, 0x7fff },  /* HP Envy 17 -> ALC287_FIXUP_CS35L41_I2C_2 (nothing to do) */
  { 0x103c, 0x8bdf, 0xffff, 0x7fff },  /* HP Envy 15 -> ALC287_FIXUP_CS35L41_I2C_2 (nothing to do) */
  { 0x103c, 0x8be0, 0xffff, 0x7fff },  /* HP Envy 15 -> ALC287_FIXUP_CS35L41_I2C_2 (nothing to do) */
  { 0x103c, 0x8be1, 0xffff, 0x7fff },  /* HP Envy 15 -> ALC287_FIXUP_CS35L41_I2C_2 (nothing to do) */
  { 0x103c, 0x8be2, 0xffff, 0x7fff },  /* HP Envy 15 -> ALC287_FIXUP_CS35L41_I2C_2 (nothing to do) */
  { 0x103c, 0x8be3, 0xffff, 0x7fff },  /* HP Envy 15 -> ALC287_FIXUP_CS35L41_I2C_2 (nothing to do) */
  { 0x103c, 0x8be5, 0xffff, 0x7fff },  /* HP Envy 16 -> ALC287_FIXUP_CS35L41_I2C_2 (nothing to do) */
  { 0x103c, 0x8be6, 0xffff, 0x7fff },  /* HP Envy 16 -> ALC287_FIXUP_CS35L41_I2C_2 (nothing to do) */
  { 0x103c, 0x8be7, 0xffff, 0x7fff },  /* HP Envy 17 -> ALC287_FIXUP_CS35L41_I2C_2 (nothing to do) */
  { 0x103c, 0x8be8, 0xffff, 0x7fff },  /* HP Envy 17 -> ALC287_FIXUP_CS35L41_I2C_2 (nothing to do) */
  { 0x103c, 0x8be9, 0xffff, 0x7fff },  /* HP Envy x360 2-in-1 Laptop 15-fh0xxx -> ALC245_FIXUP_HP_ENVY_X360_15_FH0XXX (nothing to do) */
  { 0x103c, 0x8bf0, 0xffff, 0x7fff },  /* HP -> ALC236_FIXUP_HP_GPIO_LED (nothing to do) */
  { 0x103c, 0x8bfd, 0xffff, 0x7fff },  /* HP Pavilion 15-eg3xxx -> ALC287_FIXUP_HP_GPIO_LED (nothing to do) */
  { 0x103c, 0x8c15, 0xffff, 0x7fff },  /* HP Spectre x360 2-in-1 Laptop 14-eu0xxx -> ALC245_FIXUP_HP_SPECTRE_X360_EU0XXX (nothing to do) */
  { 0x103c, 0x8c16, 0xffff, 0x7fff },  /* HP Spectre x360 2-in-1 Laptop 16-aa0xxx -> ALC245_FIXUP_HP_SPECTRE_X360_16_AA0XXX (nothing to do) */
  { 0x103c, 0x8c17, 0xffff, 0x7fff },  /* HP Spectre x360 2-in-1 Laptop 16-aa0xxx -> ALC245_FIXUP_HP_SPECTRE_X360_16_AA0XXX (nothing to do) */
  { 0x103c, 0x8c21, 0xffff, 0x7fff },  /* HP Pavilion Plus Laptop 14-ey0XXX -> ALC245_FIXUP_HP_X360_MUTE_LEDS (nothing to do) */
  { 0x103c, 0x8c2d, 0xffff, 0x7fff },  /* HP Victus 15-fa1xxx (MB 8C2D) -> ALC245_FIXUP_HP_MUTE_LED_COEFBIT (nothing to do) */
  { 0x103c, 0x8c2f, 0xffff, 0x7fff },  /* HP Victus 15-fb2xxx -> ALC245_FIXUP_HP_MUTE_LED_COEFBIT (nothing to do) */
  { 0x103c, 0x8c30, 0xffff, 0x7fff },  /* HP Victus 15-fb1xxx -> ALC245_FIXUP_HP_MUTE_LED_COEFBIT (nothing to do) */
  { 0x103c, 0x8c3f, 0xffff, 0x7fff },  /* HP Victus 15-fa1xxx (MB 8C3F) -> ALC245_FIXUP_HP_MUTE_LED_COEFBIT (nothing to do) */
  { 0x103c, 0x8c46, 0xffff, 0x7fff },  /* HP EliteBook 830 G11 -> ALC245_FIXUP_CS35L41_SPI_2_HP_GPIO_LED (nothing to do) */
  { 0x103c, 0x8c47, 0xffff, 0x7fff },  /* HP EliteBook 840 G11 -> ALC245_FIXUP_CS35L41_SPI_2_HP_GPIO_LED (nothing to do) */
  { 0x103c, 0x8c48, 0xffff, 0x7fff },  /* HP EliteBook 860 G11 -> ALC245_FIXUP_CS35L41_SPI_2_HP_GPIO_LED (nothing to do) */
  { 0x103c, 0x8c49, 0xffff, 0x7fff },  /* HP Elite x360 830 2-in-1 G11 -> ALC245_FIXUP_CS35L41_SPI_2_HP_GPIO_LED (nothing to do) */
  { 0x103c, 0x8c4d, 0xffff, 0x7fff },  /* HP Omen -> ALC287_FIXUP_CS35L41_I2C_2 (nothing to do) */
  { 0x103c, 0x8c4e, 0xffff, 0x7fff },  /* HP Omen -> ALC287_FIXUP_CS35L41_I2C_2 (nothing to do) */
  { 0x103c, 0x8c4f, 0xffff, 0x7fff },  /* HP Envy 15 -> ALC287_FIXUP_CS35L41_I2C_2 (nothing to do) */
  { 0x103c, 0x8c50, 0xffff, 0x7fff },  /* HP Envy 17 -> ALC287_FIXUP_CS35L41_I2C_2 (nothing to do) */
  { 0x103c, 0x8c51, 0xffff, 0x7fff },  /* HP Envy 17 -> ALC287_FIXUP_CS35L41_I2C_2 (nothing to do) */
  { 0x103c, 0x8c52, 0xffff, 0x7fff },  /* HP EliteBook 1040 G11 -> ALC285_FIXUP_HP_GPIO_LED (nothing to do) */
  { 0x103c, 0x8c53, 0xffff, 0x7fff },  /* HP Elite x360 1040 2-in-1 G11 -> ALC285_FIXUP_HP_GPIO_LED (nothing to do) */
  { 0x103c, 0x8c66, 0xffff, 0x7fff },  /* HP Envy 16 -> ALC287_FIXUP_CS35L41_I2C_2 (nothing to do) */
  { 0x103c, 0x8c67, 0xffff, 0x7fff },  /* HP Envy 17 -> ALC287_FIXUP_CS35L41_I2C_2 (nothing to do) */
  { 0x103c, 0x8c68, 0xffff, 0x7fff },  /* HP Envy 17 -> ALC287_FIXUP_CS35L41_I2C_2 (nothing to do) */
  { 0x103c, 0x8c6a, 0xffff, 0x7fff },  /* HP Envy 16 -> ALC287_FIXUP_CS35L41_I2C_2 (nothing to do) */
  { 0x103c, 0x8c70, 0xffff, 0x7fff },  /* HP EliteBook 835 G11 -> ALC287_FIXUP_CS35L41_I2C_2_HP_GPIO_LED (nothing to do) */
  { 0x103c, 0x8c71, 0xffff, 0x7fff },  /* HP EliteBook 845 G11 -> ALC287_FIXUP_CS35L41_I2C_2_HP_GPIO_LED (nothing to do) */
  { 0x103c, 0x8c72, 0xffff, 0x7fff },  /* HP EliteBook 865 G11 -> ALC287_FIXUP_CS35L41_I2C_2_HP_GPIO_LED (nothing to do) */
  { 0x103c, 0x8c7b, 0xffff, 0x7fff },  /* HP ProBook 445 G11 -> ALC236_FIXUP_HP_MUTE_LED_MICMUTE_VREF (nothing to do) */
  { 0x103c, 0x8c7c, 0xffff, 0x7fff },  /* HP ProBook 445 G11 -> ALC236_FIXUP_HP_MUTE_LED_MICMUTE_VREF (nothing to do) */
  { 0x103c, 0x8c7d, 0xffff, 0x7fff },  /* HP ProBook 465 G11 -> ALC236_FIXUP_HP_MUTE_LED_MICMUTE_VREF (nothing to do) */
  { 0x103c, 0x8c7e, 0xffff, 0x7fff },  /* HP ProBook 465 G11 -> ALC236_FIXUP_HP_MUTE_LED_MICMUTE_VREF (nothing to do) */
  { 0x103c, 0x8c7f, 0xffff, 0x7fff },  /* HP EliteBook 645 G11 -> ALC236_FIXUP_HP_MUTE_LED_MICMUTE_VREF (nothing to do) */
  { 0x103c, 0x8c80, 0xffff, 0x7fff },  /* HP EliteBook 645 G11 -> ALC236_FIXUP_HP_MUTE_LED_MICMUTE_VREF (nothing to do) */
  { 0x103c, 0x8c81, 0xffff, 0x7fff },  /* HP EliteBook 665 G11 -> ALC236_FIXUP_HP_MUTE_LED_MICMUTE_VREF (nothing to do) */
  { 0x103c, 0x8c89, 0xffff, 0x7fff },  /* HP ProBook 460 G11 -> ALC236_FIXUP_HP_GPIO_LED (nothing to do) */
  { 0x103c, 0x8c8a, 0xffff, 0x7fff },  /* HP EliteBook 630 -> ALC236_FIXUP_HP_GPIO_LED (nothing to do) */
  { 0x103c, 0x8c8c, 0xffff, 0x7fff },  /* HP EliteBook 660 -> ALC236_FIXUP_HP_GPIO_LED (nothing to do) */
  { 0x103c, 0x8c8d, 0xffff, 0x7fff },  /* HP ProBook 440 G11 -> ALC236_FIXUP_HP_GPIO_LED (nothing to do) */
  { 0x103c, 0x8c8e, 0xffff, 0x7fff },  /* HP ProBook 460 G11 -> ALC236_FIXUP_HP_GPIO_LED (nothing to do) */
  { 0x103c, 0x8c8f, 0xffff, 0x7fff },  /* HP EliteBook 630 G11 -> ALC236_FIXUP_HP_GPIO_LED (nothing to do) */
  { 0x103c, 0x8c90, 0xffff, 0x7fff },  /* HP EliteBook 640 -> ALC236_FIXUP_HP_GPIO_LED (nothing to do) */
  { 0x103c, 0x8c91, 0xffff, 0x7fff },  /* HP EliteBook 660 -> ALC236_FIXUP_HP_GPIO_LED (nothing to do) */
  { 0x103c, 0x8c96, 0xffff, 0x7fff },  /* HP -> ALC236_FIXUP_HP_MUTE_LED_MICMUTE_VREF (nothing to do) */
  { 0x103c, 0x8c97, 0xffff, 0x7fff },  /* HP ZBook -> ALC236_FIXUP_HP_MUTE_LED_MICMUTE_VREF (nothing to do) */
  { 0x103c, 0x8c99, 0xffff, 0x7fff },  /* HP Victus 16-r1xxx (MB 8C99) -> ALC245_FIXUP_HP_MUTE_LED_COEFBIT (nothing to do) */
  { 0x103c, 0x8c9c, 0xffff, 0x7fff },  /* HP Victus 16-s1xxx (MB 8C9C) -> ALC245_FIXUP_HP_MUTE_LED_COEFBIT (nothing to do) */
  { 0x103c, 0x8ca1, 0xffff, 0x7fff },  /* HP ZBook Power -> ALC236_FIXUP_HP_GPIO_LED (nothing to do) */
  { 0x103c, 0x8ca2, 0xffff, 0x7fff },  /* HP ZBook Power -> ALC236_FIXUP_HP_GPIO_LED (nothing to do) */
  { 0x103c, 0x8ca4, 0xffff, 0x7fff },  /* HP ZBook Fury -> ALC245_FIXUP_CS35L41_SPI_2_HP_GPIO_LED (nothing to do) */
  { 0x103c, 0x8ca7, 0xffff, 0x7fff },  /* HP ZBook Fury -> ALC245_FIXUP_CS35L41_SPI_2_HP_GPIO_LED (nothing to do) */
  { 0x103c, 0x8caf, 0xffff, 0x7fff },  /* HP Elite mt645 G8 Mobile Thin Client -> ALC236_FIXUP_HP_MUTE_LED_MICMUTE_VREF (nothing to do) */
  { 0x103c, 0x8cbc, 0xffff, 0x7fff },  /* HP Pavilion Laptop 16-ag0xxx -> ALC245_FIXUP_HP_X360_MUTE_LEDS (nothing to do) */
  { 0x103c, 0x8cbd, 0xffff, 0x7fff },  /* HP Pavilion Aero Laptop 13-bg0xxx -> ALC245_FIXUP_HP_X360_MUTE_LEDS (nothing to do) */
  { 0x103c, 0x8cdd, 0xffff, 0x7fff },  /* HP Spectre -> ALC245_FIXUP_HP_SPECTRE_X360_EU0XXX (nothing to do) */
  { 0x103c, 0x8cde, 0xffff, 0x7fff },  /* HP OmniBook Ultra Flip Laptop 14t -> ALC245_FIXUP_HP_SPECTRE_X360_EU0XXX (nothing to do) */
  { 0x103c, 0x8cdf, 0xffff, 0x7fff },  /* HP SnowWhite -> ALC287_FIXUP_CS35L41_I2C_2_HP_GPIO_LED (nothing to do) */
  { 0x103c, 0x8ce0, 0xffff, 0x7fff },  /* HP SnowWhite -> ALC287_FIXUP_CS35L41_I2C_2_HP_GPIO_LED (nothing to do) */
  { 0x103c, 0x8cf5, 0xffff, 0x7fff },  /* HP ZBook Studio 16 -> ALC245_FIXUP_CS35L41_SPI_4_HP_GPIO_LED (nothing to do) */
  { 0x103c, 0x8d01, 0xffff, 0x7fff },  /* HP ZBook Power 14 G12 -> ALC285_FIXUP_HP_GPIO_LED (nothing to do) */
  { 0x103c, 0x8d07, 0xffff, 0x7fff },  /* HP Victus 15-fb2xxx (MB 8D07) -> ALC245_FIXUP_HP_MUTE_LED_COEFBIT (nothing to do) */
  { 0x103c, 0x8d18, 0xffff, 0x7fff },  /* HP EliteStudio 8 AIO -> ALC274_FIXUP_HP_AIO_BIND_DACS (nothing to do) */
  { 0x103c, 0x8d84, 0xffff, 0x7fff },  /* HP EliteBook X G1i -> ALC285_FIXUP_HP_GPIO_LED (nothing to do) */
  { 0x103c, 0x8d85, 0xffff, 0x7fff },  /* HP EliteBook 14 G12 -> ALC285_FIXUP_HP_GPIO_LED (nothing to do) */
  { 0x103c, 0x8d86, 0xffff, 0x7fff },  /* HP Elite X360 14 G12 -> ALC285_FIXUP_HP_GPIO_LED (nothing to do) */
  { 0x103c, 0x8d8c, 0xffff, 0x7fff },  /* HP EliteBook 13 G12 -> ALC285_FIXUP_HP_GPIO_LED (nothing to do) */
  { 0x103c, 0x8d8d, 0xffff, 0x7fff },  /* HP Elite X360 13 G12 -> ALC285_FIXUP_HP_GPIO_LED (nothing to do) */
  { 0x103c, 0x8d8e, 0xffff, 0x7fff },  /* HP EliteBook 14 G12 -> ALC285_FIXUP_HP_GPIO_LED (nothing to do) */
  { 0x103c, 0x8d8f, 0xffff, 0x7fff },  /* HP EliteBook 14 G12 -> ALC285_FIXUP_HP_GPIO_LED (nothing to do) */
  { 0x103c, 0x8d90, 0xffff, 0x7fff },  /* HP EliteBook 16 G12 -> ALC285_FIXUP_HP_GPIO_LED (nothing to do) */
  { 0x103c, 0x8d91, 0xffff, 0x7fff },  /* HP ZBook Firefly 14 G12 -> ALC285_FIXUP_HP_GPIO_LED (nothing to do) */
  { 0x103c, 0x8d92, 0xffff, 0x7fff },  /* HP ZBook Firefly 16 G12 -> ALC285_FIXUP_HP_GPIO_LED (nothing to do) */
  { 0x103c, 0x8dcd, 0xffff, 0x7fff },  /* HP Victus 15-fa2xxx -> ALC245_FIXUP_HP_MUTE_LED_COEFBIT (nothing to do) */
  { 0x103c, 0x8d9b, 0xffff, 0x7fff },  /* HP 17 Turbine OmniBook 7 UMA -> ALC287_FIXUP_CS35L41_I2C_2 (nothing to do) */
  { 0x103c, 0x8d9c, 0xffff, 0x7fff },  /* HP 17 Turbine OmniBook 7 DIS -> ALC287_FIXUP_CS35L41_I2C_2_HP_MUTE_LEDS (nothing to do) */
  { 0x103c, 0x8d9d, 0xffff, 0x7fff },  /* HP 17 Turbine OmniBook X UMA -> ALC287_FIXUP_CS35L41_I2C_2 (nothing to do) */
  { 0x103c, 0x8d9e, 0xffff, 0x7fff },  /* HP 17 Turbine OmniBook X DIS -> ALC287_FIXUP_CS35L41_I2C_2 (nothing to do) */
  { 0x103c, 0x8d9f, 0xffff, 0x7fff },  /* HP 14 Cadet (x360) -> ALC287_FIXUP_CS35L41_I2C_2 (nothing to do) */
  { 0x103c, 0x8da0, 0xffff, 0x7fff },  /* HP 16 Clipper OmniBook 7(X360) -> ALC287_FIXUP_CS35L41_I2C_2 (nothing to do) */
  { 0x103c, 0x8da1, 0xffff, 0x7fff },  /* HP 16 Clipper OmniBook X -> ALC287_FIXUP_CS35L41_I2C_2 (nothing to do) */
  { 0x103c, 0x8da7, 0xffff, 0x7fff },  /* HP 14 Enstrom OmniBook X -> ALC287_FIXUP_CS35L41_I2C_2 (nothing to do) */
  { 0x103c, 0x8da8, 0xffff, 0x7fff },  /* HP 16 Piston OmniBook X -> ALC245_FIXUP_HP_ENVY_X360_15_FH0XXX (nothing to do) */
  { 0x103c, 0x8dc9, 0xffff, 0x0068 },  /* HP Laptop 15-fc0xxx -> ALC236_FIXUP_HP_DMIC */
  { 0x103c, 0x8dd4, 0xffff, 0x7fff },  /* HP EliteStudio 8 AIO -> ALC274_FIXUP_HP_AIO_BIND_DACS (nothing to do) */
  { 0x103c, 0x8dd7, 0xffff, 0x7fff },  /* HP Laptop 15-fd0xxx -> ALC236_FIXUP_HP_MUTE_LED_MICMUTE_GPIO (nothing to do) */
  { 0x103c, 0x8de8, 0xffff, 0x7fff },  /* HP Gemtree -> ALC245_FIXUP_TAS2781_SPI_2 (nothing to do) */
  { 0x103c, 0x8de9, 0xffff, 0x7fff },  /* HP Gemtree -> ALC245_FIXUP_TAS2781_SPI_2 (nothing to do) */
  { 0x103c, 0x8dec, 0xffff, 0x7fff },  /* HP EliteBook 640 G12 -> ALC236_FIXUP_HP_GPIO_LED (nothing to do) */
  { 0x103c, 0x8ded, 0xffff, 0x7fff },  /* HP EliteBook 640 G12 -> ALC236_FIXUP_HP_GPIO_LED (nothing to do) */
  { 0x103c, 0x8dee, 0xffff, 0x7fff },  /* HP EliteBook 660 G12 -> ALC236_FIXUP_HP_GPIO_LED (nothing to do) */
  { 0x103c, 0x8def, 0xffff, 0x7fff },  /* HP EliteBook 660 G12 -> ALC236_FIXUP_HP_GPIO_LED (nothing to do) */
  { 0x103c, 0x8df0, 0xffff, 0x7fff },  /* HP EliteBook 630 G12 -> ALC236_FIXUP_HP_GPIO_LED (nothing to do) */
  { 0x103c, 0x8df1, 0xffff, 0x7fff },  /* HP EliteBook 630 G12 -> ALC236_FIXUP_HP_GPIO_LED (nothing to do) */
  { 0x103c, 0x8df7, 0xffff, 0x7fff },  /* HP Z66 G6 -> ALC236_FIXUP_HP_GPIO_LED (nothing to do) */
  { 0x103c, 0x8dfb, 0xffff, 0x7fff },  /* HP EliteBook 6 G1a 14 -> ALC236_FIXUP_HP_MUTE_LED_MICMUTE_VREF (nothing to do) */
  { 0x103c, 0x8dfc, 0xffff, 0x7fff },  /* HP EliteBook 645 G12 -> ALC236_FIXUP_HP_GPIO_LED (nothing to do) */
  { 0x103c, 0x8dfd, 0xffff, 0x7fff },  /* HP EliteBook 6 G1a 16 -> ALC236_FIXUP_HP_MUTE_LED_MICMUTE_VREF (nothing to do) */
  { 0x103c, 0x8dfe, 0xffff, 0x7fff },  /* HP EliteBook 665 G12 -> ALC236_FIXUP_HP_GPIO_LED (nothing to do) */
  { 0x103c, 0x8e0d, 0xffff, 0x7fff },  /* HP EliteBook 6 G1a 14 (AD3Q9ET#UUG) -> ALC236_FIXUP_HP_MUTE_LED_MICMUTE_VREF (nothing to do) */
  { 0x103c, 0x8e11, 0xffff, 0x7fff },  /* HP Trekker -> ALC287_FIXUP_CS35L41_I2C_2 (nothing to do) */
  { 0x103c, 0x8e12, 0xffff, 0x7fff },  /* HP Trekker -> ALC287_FIXUP_CS35L41_I2C_2 (nothing to do) */
  { 0x103c, 0x8e13, 0xffff, 0x7fff },  /* HP Trekker -> ALC287_FIXUP_CS35L41_I2C_2 (nothing to do) */
  { 0x103c, 0x8e14, 0xffff, 0x7fff },  /* HP ZBook Firefly 14 G12 -> ALC245_FIXUP_HP_ZBOOK_FIREFLY_G12A (nothing to do) */
  { 0x103c, 0x8e15, 0xffff, 0x7fff },  /* HP ZBook Firefly 14 G12 -> ALC245_FIXUP_HP_ZBOOK_FIREFLY_G12A (nothing to do) */
  { 0x103c, 0x8e16, 0xffff, 0x7fff },  /* HP ZBook Firefly 14 G12 -> ALC245_FIXUP_HP_ZBOOK_FIREFLY_G12A (nothing to do) */
  { 0x103c, 0x8e17, 0xffff, 0x7fff },  /* HP ZBook Firefly 14 G12 -> ALC245_FIXUP_HP_ZBOOK_FIREFLY_G12A (nothing to do) */
  { 0x103c, 0x8e18, 0xffff, 0x7fff },  /* HP ZBook Firefly 14 G12A -> ALC245_FIXUP_HP_ZBOOK_FIREFLY_G12A (nothing to do) */
  { 0x103c, 0x8e19, 0xffff, 0x7fff },  /* HP ZBook Firefly 14 G12A -> ALC245_FIXUP_HP_ZBOOK_FIREFLY_G12A (nothing to do) */
  { 0x103c, 0x8e1a, 0xffff, 0x7fff },  /* HP ZBook Firefly 14 G12A -> ALC245_FIXUP_HP_ZBOOK_FIREFLY_G12A (nothing to do) */
  { 0x103c, 0x8e1b, 0xffff, 0x7fff },  /* HP EliteBook G12 -> ALC245_FIXUP_HP_ZBOOK_FIREFLY_G12A (nothing to do) */
  { 0x103c, 0x8e1c, 0xffff, 0x7fff },  /* HP EliteBook G12 -> ALC245_FIXUP_HP_ZBOOK_FIREFLY_G12A (nothing to do) */
  { 0x103c, 0x8e1d, 0xffff, 0x7fff },  /* HP ZBook X Gli 16 G12 -> ALC236_FIXUP_HP_GPIO_LED (nothing to do) */
  { 0x103c, 0x8e2c, 0xffff, 0x7fff },  /* HP EliteBook 16 G12 -> ALC285_FIXUP_HP_GPIO_LED (nothing to do) */
  { 0x103c, 0x8e36, 0xffff, 0x7fff },  /* HP 14 Enstrom OmniBook X -> ALC287_FIXUP_CS35L41_I2C_2 (nothing to do) */
  { 0x103c, 0x8e37, 0xffff, 0x7fff },  /* HP 16 Piston OmniBook X -> ALC287_FIXUP_CS35L41_I2C_2 (nothing to do) */
  { 0x103c, 0x8e3a, 0xffff, 0x7fff },  /* HP Agusta -> ALC287_FIXUP_CS35L41_I2C_2 (nothing to do) */
  { 0x103c, 0x8e3b, 0xffff, 0x7fff },  /* HP Agusta -> ALC287_FIXUP_CS35L41_I2C_2 (nothing to do) */
  { 0x103c, 0x8e43, 0xffff, 0x7fff },  /* HP Laptop 15-fd2xxx -> ALC236_FIXUP_HP_MUTE_LED_MICMUTE_GPIO (nothing to do) */
  { 0x103c, 0x8e60, 0xffff, 0x7fff },  /* HP OmniBook 7 Laptop 16-bh0xxx -> ALC245_FIXUP_CS35L41_I2C_2_MUTE_LED (nothing to do) */
  { 0x103c, 0x8e61, 0xffff, 0x7fff },  /* HP Trekker  -> ALC287_FIXUP_CS35L41_I2C_2 (nothing to do) */
  { 0x103c, 0x8e62, 0xffff, 0x7fff },  /* HP Trekker  -> ALC287_FIXUP_CS35L41_I2C_2 (nothing to do) */
  { 0x103c, 0x8e75, 0xffff, 0x7fff },  /* HP Trekker G7JC -> ALC287_FIXUP_CS35L41_I2C_2 (nothing to do) */
  { 0x103c, 0x8e8a, 0xffff, 0x7fff },  /* HP NexusX -> ALC245_FIXUP_HP_TAS2781_I2C_MUTE_LED (nothing to do) */
  { 0x103c, 0x8e9c, 0xffff, 0x7fff },  /* HP 16 Clipper OmniBook X X360 -> ALC287_FIXUP_CS35L41_I2C_2 (nothing to do) */
  { 0x103c, 0x8e9d, 0xffff, 0x7fff },  /* HP 17 Turbine OmniBook X UMA -> ALC287_FIXUP_CS35L41_I2C_2 (nothing to do) */
  { 0x103c, 0x8e9e, 0xffff, 0x7fff },  /* HP 17 Turbine OmniBook X UMA -> ALC287_FIXUP_CS35L41_I2C_2 (nothing to do) */
  { 0x103c, 0x8eb6, 0xffff, 0x7fff },  /* HP Abe A6U -> ALC236_FIXUP_HP_MUTE_LED_MICMUTE_GPIO (nothing to do) */
  { 0x103c, 0x8eb8, 0xffff, 0x7fff },  /* HP Abe A6U -> ALC236_FIXUP_HP_MUTE_LED_MICMUTE_GPIO (nothing to do) */
  { 0x103c, 0x8ec1, 0xffff, 0x7fff },  /* HP 200 G2i -> ALC236_FIXUP_HP_MUTE_LED_MICMUTE_GPIO (nothing to do) */
  { 0x103c, 0x8ec4, 0xffff, 0x7fff },  /* HP Bantie I6U -> ALC236_FIXUP_HP_MUTE_LED_MICMUTE_GPIO (nothing to do) */
  { 0x103c, 0x8ec5, 0xffff, 0x7fff },  /* HP Bantie I6U -> ALC236_FIXUP_HP_MUTE_LED_MICMUTE_GPIO (nothing to do) */
  { 0x103c, 0x8ece, 0xffff, 0x7fff },  /* HP Abe I6U -> ALC236_FIXUP_HP_MUTE_LED_MICMUTE_GPIO (nothing to do) */
  { 0x103c, 0x8ecf, 0xffff, 0x7fff },  /* HP Abe I6U -> ALC236_FIXUP_HP_MUTE_LED_MICMUTE_GPIO (nothing to do) */
  { 0x103c, 0x8ed2, 0xffff, 0x7fff },  /* HP Abe I6U -> ALC236_FIXUP_HP_MUTE_LED_MICMUTE_GPIO (nothing to do) */
  { 0x103c, 0x8ed5, 0xffff, 0x7fff },  /* HP EliteBook 8 Flip G2i 13 -> ALC245_FIXUP_HP_TAS2781_SPI_MUTE_LED (nothing to do) */
  { 0x103c, 0x8ed6, 0xffff, 0x7fff },  /* HP EliteBook 8 G2i 13 -> ALC245_FIXUP_HP_TAS2781_SPI_MUTE_LED (nothing to do) */
  { 0x103c, 0x8ed7, 0xffff, 0x7fff },  /* HP EliteBook 8 G2i 14 -> ALC245_FIXUP_HP_TAS2781_SPI_MUTE_LED (nothing to do) */
  { 0x103c, 0x8ed8, 0xffff, 0x7fff },  /* HP EliteBook 8 G2i 16 -> ALC245_FIXUP_HP_TAS2781_SPI_MUTE_LED (nothing to do) */
  { 0x103c, 0x8ed9, 0xffff, 0x7fff },  /* HP ZBook Firefly 14W -> ALC245_FIXUP_HP_TAS2781_SPI_MUTE_LED (nothing to do) */
  { 0x103c, 0x8eda, 0xffff, 0x7fff },  /* HP ZBook Firefly 16W -> ALC245_FIXUP_HP_TAS2781_SPI_MUTE_LED (nothing to do) */
  { 0x103c, 0x8ee4, 0xffff, 0x7fff },  /* HP Bantie A6U -> ALC236_FIXUP_HP_MUTE_LED_MICMUTE_GPIO (nothing to do) */
  { 0x103c, 0x8ee5, 0xffff, 0x7fff },  /* HP Bantie A6U -> ALC236_FIXUP_HP_MUTE_LED_MICMUTE_GPIO (nothing to do) */
  { 0x103c, 0x8ee7, 0xffff, 0x7fff },  /* HP Abe A6U -> ALC236_FIXUP_HP_MUTE_LED_MICMUTE_GPIO (nothing to do) */
  { 0x103c, 0x8f07, 0xffff, 0x7fff },  /* HP Agusta G7KX -> ALC287_FIXUP_CS35L41_I2C_2 (nothing to do) */
  { 0x103c, 0x8f0c, 0xffff, 0x7fff },  /* HP ZBook X G2i 16W -> ALC236_FIXUP_HP_GPIO_LED (nothing to do) */
  { 0x103c, 0x8f0e, 0xffff, 0x7fff },  /* HP ZBook X G2i 16W -> ALC236_FIXUP_HP_GPIO_LED (nothing to do) */
  { 0x103c, 0x8f2d, 0xffff, 0x7fff },  /* HP Auster 14 -> ALC287_FIXUP_CS35L41_I2C_2 (nothing to do) */
  { 0x103c, 0x8f2e, 0xffff, 0x7fff },  /* HP Auster 14 -> ALC287_FIXUP_CS35L41_I2C_2 (nothing to do) */
  { 0x103c, 0x8f37, 0xffff, 0x7fff },  /* HP EliteBook 6 G2i -> ALC236_FIXUP_HP_GPIO_LED (nothing to do) */
  { 0x103c, 0x8f38, 0xffff, 0x7fff },  /* HP EliteBook 6 G2i -> ALC236_FIXUP_HP_GPIO_LED (nothing to do) */
  { 0x103c, 0x8f39, 0xffff, 0x7fff },  /* HP EliteBook 6 G2i -> ALC236_FIXUP_HP_GPIO_LED (nothing to do) */
  { 0x103c, 0x8f3a, 0xffff, 0x7fff },  /* HP EliteBook 6 G2i -> ALC236_FIXUP_HP_GPIO_LED (nothing to do) */
  { 0x103c, 0x8f3c, 0xffff, 0x7fff },  /* HP EliteBook 6 G2a -> ALC236_FIXUP_HP_MUTE_LED_MICMUTE_VREF (nothing to do) */
  { 0x103c, 0x8f3d, 0xffff, 0x7fff },  /* HP EliteBook 6 G2a -> ALC236_FIXUP_HP_MUTE_LED_MICMUTE_VREF (nothing to do) */
  { 0x103c, 0x8f40, 0xffff, 0x7fff },  /* HP ZBook 8 G2a 14 -> ALC245_FIXUP_HP_TAS2781_I2C_MUTE_LED (nothing to do) */
  { 0x103c, 0x8f41, 0xffff, 0x7fff },  /* HP ZBook 8 G2a 16 -> ALC245_FIXUP_HP_TAS2781_I2C_MUTE_LED (nothing to do) */
  { 0x103c, 0x8f42, 0xffff, 0x7fff },  /* HP ZBook 8 G2a 14W -> ALC245_FIXUP_HP_TAS2781_I2C_MUTE_LED (nothing to do) */
  { 0x103c, 0x8f57, 0xffff, 0x7fff },  /* HP Trekker G7JC -> ALC287_FIXUP_CS35L41_I2C_2 (nothing to do) */
  { 0x103c, 0x8f62, 0xffff, 0x7fff },  /* HP ZBook 8 G2a 16W -> ALC245_FIXUP_HP_TAS2781_I2C_MUTE_LED (nothing to do) */
  { 0x103c, 0x8f7e, 0xffff, 0x7fff },  /* HP Messi -> ALC245_FIXUP_HP_TAS2781_I2C_MUTE_LED (nothing to do) */
  { 0x103c, 0x8f94, 0xffff, 0x7fff },  /* HP ZBook 8 G2a 14 -> ALC245_FIXUP_HP_TAS2781_I2C_MUTE_LED_INVERTED (nothing to do) */
  { 0x103c, 0x8f95, 0xffff, 0x7fff },  /* HP ZBook 8 G2a 16 -> ALC245_FIXUP_HP_TAS2781_I2C_MUTE_LED_INVERTED (nothing to do) */
  { 0x1043, 0x1024, 0xffff, 0x0069 },  /* ASUS Zephyrus G14 2025 -> ALC285_FIXUP_ASUS_GA403U_HEADSET_MIC */
  { 0x1043, 0x1032, 0xffff, 0x006c },  /* ASUS VivoBook X513EA -> ALC256_FIXUP_ASUS_MIC_NO_PRESENCE */
  { 0x1043, 0x1034, 0xffff, 0x006d },  /* ASUS GU605C -> ALC285_FIXUP_ASUS_GU605_SPI_SPEAKER2_TO_DAC1 */
  { 0x1043, 0x103e, 0xffff, 0x006f },  /* ASUS X540SA -> ALC256_FIXUP_ASUS_MIC */
  { 0x1043, 0x103f, 0xffff, 0x7fff },  /* ASUS TX300 -> ALC282_FIXUP_ASUS_TX300 (nothing to do) */
  { 0x1043, 0x1044, 0xffff, 0x0069 },  /* ASUS GA403UM -> ALC285_FIXUP_ASUS_GA403U_HEADSET_MIC */
  { 0x1043, 0x1054, 0xffff, 0x7fff },  /* ASUS G614FH/FM/FP -> ALC287_FIXUP_CS35L41_I2C_2 (nothing to do) */
  { 0x1043, 0x106d, 0xffff, 0x7fff },  /* Asus K53BE -> ALC269_FIXUP_LIMIT_INT_MIC_BOOST (nothing to do) */
  { 0x1043, 0x106f, 0xffff, 0x006c },  /* ASUS VivoBook X515UA -> ALC256_FIXUP_ASUS_MIC_NO_PRESENCE */
  { 0x1043, 0x1074, 0xffff, 0x7fff },  /* ASUS G614PH/PM/PP -> ALC287_FIXUP_CS35L41_I2C_2 (nothing to do) */
  { 0x1043, 0x10a1, 0xffff, 0x0070 },  /* ASUS UX391UA -> ALC294_FIXUP_ASUS_SPK */
  { 0x1043, 0x10a4, 0xffff, 0x0072 },  /* ASUS TP3407SA -> ALC287_FIXUP_TAS2781_I2C */
  { 0x1043, 0x10c0, 0xffff, 0x006f },  /* ASUS X540SA -> ALC256_FIXUP_ASUS_MIC */
  { 0x1043, 0x10c4, 0xffff, 0x7fff },  /* ASUS Vivobook S14 S5406SA -> ALC2XX_FIXUP_HEADSET_MIC (nothing to do) */
  { 0x1043, 0x10d0, 0xffff, 0x0077 },  /* ASUS X540LA/X540LJ -> ALC255_FIXUP_ASUS_MIC_NO_PRESENCE */
  { 0x1043, 0x10d3, 0xffff, 0x0070 },  /* ASUS K6500ZC -> ALC294_FIXUP_ASUS_SPK */
  { 0x1043, 0x1154, 0xffff, 0x0072 },  /* ASUS TP3607SH -> ALC287_FIXUP_TAS2781_I2C */
  { 0x1043, 0x115d, 0xffff, 0x7fff },  /* Asus 1015E -> ALC269_FIXUP_LIMIT_INT_MIC_BOOST (nothing to do) */
  { 0x1043, 0x1194, 0xffff, 0x7fff },  /* ASUS UM3406KA -> ALC287_FIXUP_CS35L41_I2C_2 (nothing to do) */
  { 0x1043, 0x11c0, 0xffff, 0x0077 },  /* ASUS X556UR -> ALC255_FIXUP_ASUS_MIC_NO_PRESENCE */
  { 0x1043, 0x1204, 0xffff, 0x0078 },  /* ASUS Strix G615JHR_JMR_JPR -> ALC287_FIXUP_TXNW2781_I2C_ASUS */
  { 0x1043, 0x1214, 0xffff, 0x0078 },  /* ASUS Strix G615LH_LM_LP -> ALC287_FIXUP_TXNW2781_I2C_ASUS */
  { 0x1043, 0x125e, 0xffff, 0x0077 },  /* ASUS Q524UQK -> ALC255_FIXUP_ASUS_MIC_NO_PRESENCE */
  { 0x1043, 0x1271, 0xffff, 0x006c },  /* ASUS X430UN -> ALC256_FIXUP_ASUS_MIC_NO_PRESENCE */
  { 0x1043, 0x1290, 0xffff, 0x0079 },  /* ASUS X441SA -> ALC233_FIXUP_EAPD_COEF_AND_MIC_NO_PRESENCE */
  { 0x1043, 0x1294, 0xffff, 0x7fff },  /* ASUS B3405CVA -> ALC245_FIXUP_CS35L41_SPI_2 (nothing to do) */
  { 0x1043, 0x12a0, 0xffff, 0x0079 },  /* ASUS X441UV -> ALC233_FIXUP_EAPD_COEF_AND_MIC_NO_PRESENCE */
  { 0x1043, 0x12a3, 0xffff, 0x007a },  /* Asus N7691ZM -> ALC269_FIXUP_ASUS_N7601ZM */
  { 0x1043, 0x12af, 0xffff, 0x7fff },  /* ASUS UX582ZS -> ALC245_FIXUP_CS35L41_SPI_2 (nothing to do) */
  { 0x1043, 0x12b4, 0xffff, 0x007c },  /* ASUS B3405CCA / P3405CCA -> ALC294_FIXUP_ASUS_CS35L41_SPI_2 */
  { 0x1043, 0x12e0, 0xffff, 0x006c },  /* ASUS X541SA -> ALC256_FIXUP_ASUS_MIC_NO_PRESENCE */
  { 0x1043, 0x12f0, 0xffff, 0x006c },  /* ASUS X541UV -> ALC256_FIXUP_ASUS_MIC_NO_PRESENCE */
  { 0x1043, 0x1313, 0xffff, 0x007d },  /* Asus K42JZ -> ALC269VB_FIXUP_ASUS_MIC_NO_PRESENCE */
  { 0x1043, 0x1314, 0xffff, 0x007e },  /* ASUS GA605K -> ALC285_FIXUP_ASUS_GA605K_HEADSET_MIC */
  { 0x1043, 0x1384, 0xffff, 0x0078 },  /* ASUS RC73XA -> ALC287_FIXUP_TXNW2781_I2C_ASUS */
  { 0x1043, 0x1394, 0xffff, 0x0078 },  /* ASUS RC73YA -> ALC287_FIXUP_TXNW2781_I2C_ASUS */
  { 0x1043, 0x13b0, 0xffff, 0x006c },  /* ASUS Z550SA -> ALC256_FIXUP_ASUS_MIC_NO_PRESENCE */
  { 0x1043, 0x1427, 0xffff, 0x0080 },  /* Asus Zenbook UX31E -> ALC269VB_FIXUP_ASUS_ZENBOOK */
  { 0x1043, 0x1433, 0xffff, 0x0082 },  /* ASUS GX650PY/PZ/PV/PU/PYV/PZV/PIV/PVV -> ALC285_FIXUP_ASUS_I2C_HEADSET_MIC */
  { 0x1043, 0x1454, 0xffff, 0x7fff },  /* ASUS PM3406CKA -> ALC287_FIXUP_CS35L41_I2C_2 (nothing to do) */
  { 0x1043, 0x1460, 0xffff, 0x006c },  /* Asus VivoBook 15 -> ALC256_FIXUP_ASUS_MIC_NO_PRESENCE */
  { 0x1043, 0x1463, 0xffff, 0x0082 },  /* Asus GA402X/GA402N -> ALC285_FIXUP_ASUS_I2C_HEADSET_MIC */
  { 0x1043, 0x1473, 0xffff, 0x0085 },  /* ASUS GU604VI/VC/VE/VG/VJ/VQ/VU/VV/VY/VZ -> ALC285_FIXUP_ASUS_HEADSET_MIC */
  { 0x1043, 0x1483, 0xffff, 0x0085 },  /* ASUS GU603VQ/VU/VV/VJ/VI -> ALC285_FIXUP_ASUS_HEADSET_MIC */
  { 0x1043, 0x1493, 0xffff, 0x0085 },  /* ASUS GV601VV/VU/VJ/VQ/VI -> ALC285_FIXUP_ASUS_HEADSET_MIC */
  { 0x1043, 0x14d3, 0xffff, 0x7fff },  /* ASUS G614JY/JZ/JG -> ALC245_FIXUP_CS35L41_SPI_2 (nothing to do) */
  { 0x1043, 0x14e3, 0xffff, 0x7fff },  /* ASUS G513PI/PU/PV -> ALC287_FIXUP_CS35L41_I2C_2 (nothing to do) */
  { 0x1043, 0x14f2, 0xffff, 0x006c },  /* ASUS VivoBook X515JA -> ALC256_FIXUP_ASUS_MIC_NO_PRESENCE */
  { 0x1043, 0x1503, 0xffff, 0x7fff },  /* ASUS G733PY/PZ/PZV/PYV -> ALC287_FIXUP_CS35L41_I2C_2 (nothing to do) */
  { 0x1043, 0x1514, 0xffff, 0x7fff },  /* ASUS ROG Flow Z13 GZ302EAC -> ALC287_FIXUP_CS35L41_I2C_2 (nothing to do) */
  { 0x1043, 0x1517, 0xffff, 0x0088 },  /* Asus Zenbook UX31A -> ALC269VB_FIXUP_ASUS_ZENBOOK_UX31A */
  { 0x1043, 0x1533, 0xffff, 0x7fff },  /* ASUS GV302XA/XJ/XQ/XU/XV/XI -> ALC287_FIXUP_CS35L41_I2C_2 (nothing to do) */
  { 0x1043, 0x1573, 0xffff, 0x0085 },  /* ASUS GZ301VV/VQ/VU/VJ/VA/VC/VE/VVC/VQC/VUC/VJC/VEC/VCC -> ALC285_FIXUP_ASUS_HEADSET_MIC */
  { 0x1043, 0x1584, 0xffff, 0x7fff },  /* ASUS UM3406GA  -> ALC287_FIXUP_CS35L41_I2C_2 (nothing to do) */
  { 0x1043, 0x1602, 0xffff, 0x0089 },  /* ASUS ROG Strix SCAR 15 -> ALC285_FIXUP_ASUS_G533Z_PINS */
  { 0x1043, 0x1652, 0xffff, 0x008a },  /* ASUS ROG Zephyrus Do 15 SE -> ALC289_FIXUP_ASUS_ZEPHYRUS_DUAL_SPK */
  { 0x1043, 0x1662, 0xffff, 0x008b },  /* ASUS GV301QH -> ALC294_FIXUP_ASUS_DUAL_SPK */
  { 0x1043, 0x1663, 0xffff, 0x0085 },  /* ASUS GU603ZI/ZJ/ZQ/ZU/ZV -> ALC285_FIXUP_ASUS_HEADSET_MIC */
  { 0x1043, 0x1664, 0xffff, 0x008d },  /* ASUS ExpertBook PM3406CHA -> ALC256_FIXUP_ASUS_PM3406CHA */
  { 0x1043, 0x1683, 0xffff, 0x7fff },  /* ASUS UM3402YAR -> ALC287_FIXUP_CS35L41_I2C_2 (nothing to do) */
  { 0x1043, 0x16a3, 0xffff, 0x7fff },  /* ASUS UX3402VA -> ALC245_FIXUP_CS35L41_SPI_2 (nothing to do) */
  { 0x1043, 0x16b2, 0xffff, 0x008e },  /* ASUS GU603 -> ALC289_FIXUP_ASUS_GA401 */
  { 0x1043, 0x16d3, 0xffff, 0x7fff },  /* ASUS UX5304VA -> ALC245_FIXUP_CS35L41_SPI_2 (nothing to do) */
  { 0x1043, 0x16e3, 0xffff, 0x7fff },  /* ASUS UX50 -> ALC269_FIXUP_STEREO_DMIC (nothing to do) */
  { 0x1043, 0x16f3, 0xffff, 0x7fff },  /* ASUS UX7602VI/BZ -> ALC245_FIXUP_CS35L41_SPI_2 (nothing to do) */
  { 0x1043, 0x1740, 0xffff, 0x7fff },  /* ASUS UX430UA -> ALC295_FIXUP_ASUS_DACS (nothing to do) */
  { 0x1043, 0x17d1, 0xffff, 0x008b },  /* ASUS UX431FL -> ALC294_FIXUP_ASUS_DUAL_SPK */
  { 0x1043, 0x17f3, 0xffff, 0x008f },  /* ROG Ally NR2301L/X -> ALC294_FIXUP_ASUS_ALLY */
  { 0x1043, 0x1863, 0xffff, 0x7fff },  /* ASUS UX6404VI/VV -> ALC245_FIXUP_CS35L41_SPI_2 (nothing to do) */
  { 0x1043, 0x1881, 0xffff, 0x0092 },  /* ASUS Zephyrus S/M -> ALC294_FIXUP_ASUS_GX502_PINS */
  { 0x1043, 0x18b1, 0xffff, 0x0095 },  /* Asus MJ401TA -> ALC256_FIXUP_ASUS_HEADSET_MIC */
  { 0x1043, 0x18d3, 0xffff, 0x7fff },  /* ASUS UM3504DA -> ALC287_FIXUP_CS35L41_I2C_2 (nothing to do) */
  { 0x1043, 0x18f1, 0xffff, 0x0095 },  /* Asus FX505DT -> ALC256_FIXUP_ASUS_HEADSET_MIC */
  { 0x1043, 0x194e, 0xffff, 0x0096 },  /* ASUS UX563FD -> ALC294_FIXUP_ASUS_HPE */
  { 0x1043, 0x1970, 0xffff, 0x008e },  /* ASUS UX550VE -> ALC289_FIXUP_ASUS_GA401 */
  { 0x1043, 0x197e, 0xffff, 0x006c },  /* ASUS VivoBook X509DAP -> ALC256_FIXUP_ASUS_MIC_NO_PRESENCE */
  { 0x1043, 0x1982, 0xffff, 0x0097 },  /* ASUS B1400CEPE -> ALC256_FIXUP_ASUS_HPE */
  { 0x1043, 0x19ce, 0xffff, 0x0096 },  /* ASUS B9450FA -> ALC294_FIXUP_ASUS_HPE */
  { 0x1043, 0x19e1, 0xffff, 0x0098 },  /* ASUS UX581LV -> ALC295_FIXUP_ASUS_MIC_NO_PRESENCE */
  { 0x1043, 0x19f4, 0xffff, 0x0099 },  /* ASUS UM3405GA -> ALC294_FIXUP_ASUS_I2C_HEADSET_MIC */
  { 0x1043, 0x1a13, 0xffff, 0x009a },  /* Asus G73Jw -> ALC269_FIXUP_ASUS_G73JW */
  { 0x1043, 0x1a63, 0xffff, 0x009b },  /* ASUS UX3405MA -> ALC294_FIXUP_ASUS_SPI_HEADSET_MIC */
  { 0x1043, 0x1a83, 0xffff, 0x7fff },  /* ASUS UM5302LA -> ALC287_FIXUP_CS35L41_I2C_2 (nothing to do) */
  { 0x1043, 0x1a8e, 0xffff, 0x009c },  /* ASUS G712LWS -> ALC294_FIXUP_LENOVO_MIC_LOCATION */
  { 0x1043, 0x1a8f, 0xffff, 0x7fff },  /* ASUS UX582ZS -> ALC245_FIXUP_CS35L41_SPI_2 (nothing to do) */
  { 0x1043, 0x1b11, 0xffff, 0x009d },  /* ASUS UX431DA -> ALC294_FIXUP_ASUS_COEF_1B */
  { 0x1043, 0x1b13, 0xffff, 0x0069 },  /* ASUS U41SV/GA403U -> ALC285_FIXUP_ASUS_GA403U_HEADSET_MIC */
  { 0x1043, 0x1b93, 0xffff, 0x7fff },  /* ASUS G614JVR/JIR -> ALC245_FIXUP_CS35L41_SPI_2 (nothing to do) */
  { 0x1043, 0x1bbd, 0xffff, 0x0077 },  /* ASUS Z550MA -> ALC255_FIXUP_ASUS_MIC_NO_PRESENCE */
  { 0x1043, 0x1c03, 0xffff, 0x0099 },  /* ASUS UM3406HA -> ALC294_FIXUP_ASUS_I2C_HEADSET_MIC */
  { 0x1043, 0x1c23, 0xffff, 0x7fff },  /* Asus X55U -> ALC269_FIXUP_LIMIT_INT_MIC_BOOST (nothing to do) */
  { 0x1043, 0x1c33, 0xffff, 0x7fff },  /* ASUS UX5304MA -> ALC245_FIXUP_CS35L41_SPI_2 (nothing to do) */
  { 0x1043, 0x1c43, 0xffff, 0x7fff },  /* ASUS UX8406MA -> ALC245_FIXUP_CS35L41_SPI_2 (nothing to do) */
  { 0x1043, 0x1c62, 0xffff, 0x008e },  /* ASUS GU603 -> ALC289_FIXUP_ASUS_GA401 */
  { 0x1043, 0x1c63, 0xffff, 0x006d },  /* ASUS GU605M -> ALC285_FIXUP_ASUS_GU605_SPI_SPEAKER2_TO_DAC1 */
  { 0x1043, 0x1c80, 0xffff, 0x006c },  /* ASUS VivoBook TP401 -> ALC256_FIXUP_ASUS_MIC_NO_PRESENCE */
  { 0x1043, 0x1c92, 0xffff, 0x0089 },  /* ASUS ROG Strix G15 -> ALC285_FIXUP_ASUS_G533Z_PINS */
  { 0x1043, 0x1c9f, 0xffff, 0x0085 },  /* ASUS G614JU/JV/JI -> ALC285_FIXUP_ASUS_HEADSET_MIC */
  { 0x1043, 0x1caf, 0xffff, 0x009e },  /* ASUS G634JY/JZ/JI/JG -> ALC285_FIXUP_ASUS_SPI_REAR_SPEAKERS */
  { 0x1043, 0x1ccd, 0xffff, 0x006c },  /* ASUS X555UB -> ALC256_FIXUP_ASUS_MIC_NO_PRESENCE */
  { 0x1043, 0x1ccf, 0xffff, 0x7fff },  /* ASUS G814JU/JV/JI -> ALC245_FIXUP_CS35L41_SPI_2 (nothing to do) */
  { 0x1043, 0x1cdf, 0xffff, 0x7fff },  /* ASUS G814JY/JZ/JG -> ALC245_FIXUP_CS35L41_SPI_2 (nothing to do) */
  { 0x1043, 0x1cef, 0xffff, 0x0085 },  /* ASUS G834JY/JZ/JI/JG -> ALC285_FIXUP_ASUS_HEADSET_MIC */
  { 0x1043, 0x1d1f, 0xffff, 0x7fff },  /* ASUS G713PI/PU/PV/PVN -> ALC287_FIXUP_CS35L41_I2C_2 (nothing to do) */
  { 0x1043, 0x1d42, 0xffff, 0x008e },  /* ASUS Zephyrus G14 2022 -> ALC289_FIXUP_ASUS_GA401 */
  { 0x1043, 0x1d4e, 0xffff, 0x0097 },  /* ASUS TM420 -> ALC256_FIXUP_ASUS_HPE */
  { 0x1043, 0x1da2, 0xffff, 0x7fff },  /* ASUS UP6502ZA/ZD -> ALC245_FIXUP_CS35L41_SPI_2 (nothing to do) */
  { 0x1043, 0x1df3, 0xffff, 0x7fff },  /* ASUS UM5606WA -> ALC294_FIXUP_BASS_SPEAKER_15 (nothing to do) */
  { 0x1043, 0x1264, 0xffff, 0x7fff },  /* ASUS UM5606KA -> ALC294_FIXUP_BASS_SPEAKER_15 (nothing to do) */
  { 0x1043, 0x1e02, 0xffff, 0x7fff },  /* ASUS UX3402ZA -> ALC245_FIXUP_CS35L41_SPI_2 (nothing to do) */
  { 0x1043, 0x1e10, 0xffff, 0x006c },  /* ASUS VivoBook X507UAR -> ALC256_FIXUP_ASUS_MIC_NO_PRESENCE */
  { 0x1043, 0x1e3e, 0xffff, 0x006c },  /* ASUS VivoBook M515DA/X515DAP -> ALC256_FIXUP_ASUS_MIC_NO_PRESENCE */
  { 0x1043, 0x1e11, 0xffff, 0x0020 },  /* ASUS Zephyrus G15 -> ALC289_FIXUP_ASUS_GA502 */
  { 0x1043, 0x1e12, 0xffff, 0x7fff },  /* ASUS UM3402 -> ALC287_FIXUP_CS35L41_I2C_2 (nothing to do) */
  { 0x1043, 0x1e1f, 0xffff, 0x7fff },  /* ASUS Vivobook 15 X1504VAP -> ALC2XX_FIXUP_HEADSET_MIC (nothing to do) */
  { 0x1043, 0x1e51, 0xffff, 0x009f },  /* ASUS Zephyrus M15 -> ALC294_FIXUP_ASUS_GU502_PINS */
  { 0x1043, 0x1e5e, 0xffff, 0x00a2 },  /* ASUS ROG Strix G513 -> ALC294_FIXUP_ASUS_G513_PINS */
  { 0x1043, 0x1e63, 0xffff, 0x006d },  /* ASUS H7606W -> ALC285_FIXUP_ASUS_GU605_SPI_SPEAKER2_TO_DAC1 */
  { 0x1043, 0x1e83, 0xffff, 0x006d },  /* ASUS GA605W -> ALC285_FIXUP_ASUS_GU605_SPI_SPEAKER2_TO_DAC1 */
  { 0x1043, 0x1e8e, 0xffff, 0x008e },  /* ASUS Zephyrus G15 -> ALC289_FIXUP_ASUS_GA401 */
  { 0x1043, 0x1e93, 0xffff, 0x0096 },  /* ASUS ExpertBook B9403CVAR -> ALC294_FIXUP_ASUS_HPE */
  { 0x1043, 0x1eb3, 0xffff, 0x00a3 },  /* ASUS Ally RC72LA -> ALC287_FIXUP_ASUS_ALLY_X */
  { 0x1043, 0x1ed3, 0xffff, 0x7fff },  /* ASUS HN7306W -> ALC287_FIXUP_CS35L41_I2C_2 (nothing to do) */
  { 0x1043, 0x1ee2, 0xffff, 0x7fff },  /* ASUS UM6702RA/RC -> ALC285_FIXUP_ASUS_I2C_SPEAKER2_TO_DAC1 (nothing to do) */
  { 0x1043, 0x1c52, 0xffff, 0x008e },  /* ASUS Zephyrus G15 2022 -> ALC289_FIXUP_ASUS_GA401 */
  { 0x1043, 0x1f11, 0xffff, 0x008e },  /* ASUS Zephyrus G14 -> ALC289_FIXUP_ASUS_GA401 */
  { 0x1043, 0x1f12, 0xffff, 0x7fff },  /* ASUS UM5302 -> ALC287_FIXUP_CS35L41_I2C_2 (nothing to do) */
  { 0x1043, 0x1f1f, 0xffff, 0x7fff },  /* ASUS H7604JI/JV/J3D -> ALC245_FIXUP_CS35L41_SPI_2 (nothing to do) */
  { 0x1043, 0x1f62, 0xffff, 0x7fff },  /* ASUS UX7602ZM -> ALC245_FIXUP_CS35L41_SPI_2 (nothing to do) */
  { 0x1043, 0x1f63, 0xffff, 0x7fff },  /* ASUS P5405CSA -> ALC245_FIXUP_CS35L41_SPI_2 (nothing to do) */
  { 0x1043, 0x1f92, 0xffff, 0x008e },  /* ASUS ROG Flow X16 -> ALC289_FIXUP_ASUS_GA401 */
  { 0x1043, 0x1fb3, 0xffff, 0x7fff },  /* ASUS ROG Flow Z13 GZ302EA -> ALC287_FIXUP_CS35L41_I2C_2 (nothing to do) */
  { 0x1043, 0x3011, 0xffff, 0x7fff },  /* ASUS B5605CVA -> ALC245_FIXUP_CS35L41_SPI_2 (nothing to do) */
  { 0x1043, 0x3030, 0xffff, 0x00a7 },  /* ASUS ZN270IE -> ALC256_FIXUP_ASUS_AIO_GPIO2 */
  { 0x1043, 0x3061, 0xffff, 0x007c },  /* ASUS B3405CCA -> ALC294_FIXUP_ASUS_CS35L41_SPI_2 */
  { 0x1043, 0x3071, 0xffff, 0x007c },  /* ASUS B5405CCA -> ALC294_FIXUP_ASUS_CS35L41_SPI_2 */
  { 0x1043, 0x30c1, 0xffff, 0x007c },  /* ASUS B3605CCA / P3605CCA -> ALC294_FIXUP_ASUS_CS35L41_SPI_2 */
  { 0x1043, 0x30d1, 0xffff, 0x007c },  /* ASUS B5405CCA -> ALC294_FIXUP_ASUS_CS35L41_SPI_2 */
  { 0x1043, 0x30e1, 0xffff, 0x007c },  /* ASUS B5605CCA -> ALC294_FIXUP_ASUS_CS35L41_SPI_2 */
  { 0x1043, 0x31d0, 0xffff, 0x00a8 },  /* ASUS Zen AIO 27 Z272SD_A272SD -> ALC274_FIXUP_ASUS_ZEN_AIO_27 */
  { 0x1043, 0x31e1, 0xffff, 0x007c },  /* ASUS B5605CCA -> ALC294_FIXUP_ASUS_CS35L41_SPI_2 */
  { 0x1043, 0x31f1, 0xffff, 0x007c },  /* ASUS B3605CCA -> ALC294_FIXUP_ASUS_CS35L41_SPI_2 */
  { 0x1043, 0x3391, 0xffff, 0x7fff },  /* ASUS PM3606CKA -> ALC287_FIXUP_CS35L41_I2C_2 (nothing to do) */
  { 0x1043, 0x3601, 0xffff, 0x7fff },  /* ASUS PM5406CGA -> ALC287_FIXUP_CS35L41_I2C_2 (nothing to do) */
  { 0x1043, 0x3611, 0xffff, 0x7fff },  /* ASUS PM5606CGA -> ALC287_FIXUP_CS35L41_I2C_2 (nothing to do) */
  { 0x1043, 0x3701, 0xffff, 0x7fff },  /* ASUS P5406CCA -> ALC245_FIXUP_CS35L41_SPI_2 (nothing to do) */
  { 0x1043, 0x3711, 0xffff, 0x7fff },  /* ASUS P5606CCA -> ALC245_FIXUP_CS35L41_SPI_2 (nothing to do) */
  { 0x1043, 0x3a20, 0xffff, 0x009e },  /* ASUS G614JZR -> ALC285_FIXUP_ASUS_SPI_REAR_SPEAKERS */
  { 0x1043, 0x3a30, 0xffff, 0x009e },  /* ASUS G814JVR/JIR -> ALC285_FIXUP_ASUS_SPI_REAR_SPEAKERS */
  { 0x1043, 0x3a40, 0xffff, 0x009e },  /* ASUS G814JZR -> ALC285_FIXUP_ASUS_SPI_REAR_SPEAKERS */
  { 0x1043, 0x3a50, 0xffff, 0x009e },  /* ASUS G834JYR/JZR -> ALC285_FIXUP_ASUS_SPI_REAR_SPEAKERS */
  { 0x1043, 0x3a60, 0xffff, 0x009e },  /* ASUS G634JYR/JZR -> ALC285_FIXUP_ASUS_SPI_REAR_SPEAKERS */
  { 0x1043, 0x3d78, 0xffff, 0x7fff },  /* ASUS GA603KH -> ALC287_FIXUP_CS35L41_I2C_2 (nothing to do) */
  { 0x1043, 0x3d88, 0xffff, 0x7fff },  /* ASUS GA603KM -> ALC287_FIXUP_CS35L41_I2C_2 (nothing to do) */
  { 0x1043, 0x3e00, 0xffff, 0x7fff },  /* ASUS G814FH/FM/FP -> ALC287_FIXUP_CS35L41_I2C_2 (nothing to do) */
  { 0x1043, 0x3e20, 0xffff, 0x7fff },  /* ASUS G814PH/PM/PP -> ALC287_FIXUP_CS35L41_I2C_2 (nothing to do) */
  { 0x1043, 0x3e30, 0xffff, 0x0072 },  /* ASUS TP3607SA -> ALC287_FIXUP_TAS2781_I2C */
  { 0x1043, 0x3ee0, 0xffff, 0x00aa },  /* ASUS Strix G815_JHR_JMR_JPR -> ALC287_FIXUP_TXNW2781_I2C */
  { 0x1043, 0x3ef0, 0xffff, 0x00aa },  /* ASUS Strix G635LR_LW_LX -> ALC287_FIXUP_TXNW2781_I2C */
  { 0x1043, 0x3f00, 0xffff, 0x00aa },  /* ASUS Strix G815LH_LM_LP -> ALC287_FIXUP_TXNW2781_I2C */
  { 0x1043, 0x3f10, 0xffff, 0x00aa },  /* ASUS Strix G835LR_LW_LX -> ALC287_FIXUP_TXNW2781_I2C */
  { 0x1043, 0x3f20, 0xffff, 0x0078 },  /* ASUS Strix G615LR_LW -> ALC287_FIXUP_TXNW2781_I2C_ASUS */
  { 0x1043, 0x3f30, 0xffff, 0x00aa },  /* ASUS Strix G815LR_LW -> ALC287_FIXUP_TXNW2781_I2C */
  { 0x1043, 0x3fd0, 0xffff, 0x7fff },  /* ASUS B3605CVA -> ALC245_FIXUP_CS35L41_SPI_2 (nothing to do) */
  { 0x1043, 0x3ff0, 0xffff, 0x7fff },  /* ASUS B5405CVA -> ALC245_FIXUP_CS35L41_SPI_2 (nothing to do) */
  { 0x1043, 0x831a, 0xffff, 0x7fff },  /* ASUS P901 -> ALC269_FIXUP_STEREO_DMIC (nothing to do) */
  { 0x1043, 0x834a, 0xffff, 0x7fff },  /* ASUS S101 -> ALC269_FIXUP_STEREO_DMIC (nothing to do) */
  { 0x1043, 0x8398, 0xffff, 0x7fff },  /* ASUS P1005 -> ALC269_FIXUP_STEREO_DMIC (nothing to do) */
  { 0x1043, 0x83ce, 0xffff, 0x7fff },  /* ASUS P1005 -> ALC269_FIXUP_STEREO_DMIC (nothing to do) */
  { 0x1043, 0x8516, 0xffff, 0x00ab },  /* ASUS X101CH -> ALC269_FIXUP_ASUS_X101 */
  { 0x1043, 0x88f4, 0xffff, 0x7fff },  /* ASUS NUC14LNS -> ALC245_FIXUP_CS35L41_SPI_1 (nothing to do) */
  { 0x104d, 0x9073, 0xffff, 0x00ae },  /* Sony VAIO -> ALC275_FIXUP_SONY_VAIO_GPIO2 */
  { 0x104d, 0x907b, 0xffff, 0x00b0 },  /* Sony VAIO -> ALC275_FIXUP_SONY_HWEQ */
  { 0x104d, 0x9084, 0xffff, 0x00b0 },  /* Sony VAIO -> ALC275_FIXUP_SONY_HWEQ */
  { 0x104d, 0x9099, 0xffff, 0x00b1 },  /* Sony VAIO S13 -> ALC275_FIXUP_SONY_DISABLE_AAMIX */
  { 0x104d, 0x90b5, 0xffff, 0x00b2 },  /* Sony VAIO Pro 11 -> ALC286_FIXUP_SONY_MIC_NO_PRESENCE */
  { 0x104d, 0x90b6, 0xffff, 0x00b2 },  /* Sony VAIO Pro 13 -> ALC286_FIXUP_SONY_MIC_NO_PRESENCE */
  { 0x10cf, 0x1475, 0xffff, 0x000c },  /* Lifebook -> ALC269_FIXUP_LIFEBOOK */
  { 0x10cf, 0x159f, 0xffff, 0x7fff },  /* Lifebook E780 -> ALC269_FIXUP_LIFEBOOK_NO_HP_TO_LINEOUT (nothing to do) */
  { 0x10cf, 0x15dc, 0xffff, 0x00b3 },  /* Lifebook T731 -> ALC269_FIXUP_LIFEBOOK_HP_PIN */
  { 0x10cf, 0x1629, 0xffff, 0x7fff },  /* Lifebook U7x7 -> ALC255_FIXUP_LIFEBOOK_U7x7_HEADSET_MIC (nothing to do) */
  { 0x10cf, 0x1757, 0xffff, 0x00b3 },  /* Lifebook E752 -> ALC269_FIXUP_LIFEBOOK_HP_PIN */
  { 0x10cf, 0x1845, 0xffff, 0x00b4 },  /* Lifebook U904 -> ALC269_FIXUP_LIFEBOOK_EXTMIC */
  { 0x10ec, 0x10f2, 0xffff, 0x00b5 },  /* Intel Reference board -> ALC700_FIXUP_INTEL_REFERENCE */
  { 0x10ec, 0x111e, 0xffff, 0x7fff },  /* Higole F9B -> ALC290_FIXUP_MONO_SPEAKERS (nothing to do) */
  { 0x10ec, 0x118c, 0xffff, 0x00b6 },  /* Medion EE4254 MD62100 -> ALC256_FIXUP_MEDION_HEADSET_NO_PRESENCE */
  { 0x10ec, 0x119e, 0xffff, 0x0002 },  /* Positivo SU C1400 -> ALC269_FIXUP_ASPIRE_HEADSET_MIC */
  { 0x10ec, 0x11bc, 0xffff, 0x7fff },  /* VAIO VJFE-IL -> ALC269_FIXUP_LIMIT_INT_MIC_BOOST (nothing to do) */
  { 0x10ec, 0x1230, 0xffff, 0x7fff },  /* Intel Reference board -> ALC295_FIXUP_CHROME_BOOK (nothing to do) */
  { 0x10ec, 0x124c, 0xffff, 0x7fff },  /* Intel Reference board -> ALC295_FIXUP_CHROME_BOOK (nothing to do) */
  { 0x10ec, 0x1252, 0xffff, 0x7fff },  /* Intel Reference board -> ALC295_FIXUP_CHROME_BOOK (nothing to do) */
  { 0x10ec, 0x1254, 0xffff, 0x7fff },  /* Intel Reference board -> ALC295_FIXUP_CHROME_BOOK (nothing to do) */
  { 0x10ec, 0x12cc, 0xffff, 0x7fff },  /* Intel Reference board -> ALC295_FIXUP_CHROME_BOOK (nothing to do) */
  { 0x10ec, 0x12f6, 0xffff, 0x7fff },  /* Intel Reference board -> ALC295_FIXUP_CHROME_BOOK (nothing to do) */
  { 0x10f7, 0x8338, 0xffff, 0x0002 },  /* Panasonic CF-SZ6 -> ALC269_FIXUP_ASPIRE_HEADSET_MIC */
  { 0x1414, 0x9c20, 0xffff, 0x7fff },  /* Microsoft Surface Pro 2/3 -> ALC288_FIXUP_SURFACE_SWAP_DACS (nothing to do) */
  { 0x144d, 0xc109, 0xffff, 0x7fff },  /* Samsung Ativ book 9 (NP900X3G) -> ALC269_FIXUP_INV_DMIC (nothing to do) */
  { 0x144d, 0xc169, 0xffff, 0x00b7 },  /* Samsung Notebook 9 Pen (NP930SBE-K01US) -> ALC298_FIXUP_SAMSUNG_AMP */
  { 0x144d, 0xc176, 0xffff, 0x00b7 },  /* Samsung Notebook 9 Pro (NP930MBE-K04US) -> ALC298_FIXUP_SAMSUNG_AMP */
  { 0x144d, 0xc188, 0xffff, 0x00b7 },  /* Samsung Galaxy Book Flex (NT950QCT-A38A) -> ALC298_FIXUP_SAMSUNG_AMP */
  { 0x144d, 0xc189, 0xffff, 0x00b7 },  /* Samsung Galaxy Book Flex (NT950QCG-X716) -> ALC298_FIXUP_SAMSUNG_AMP */
  { 0x144d, 0xc18a, 0xffff, 0x00b7 },  /* Samsung Galaxy Book Ion (NP930XCJ-K01US) -> ALC298_FIXUP_SAMSUNG_AMP */
  { 0x144d, 0xc1ac, 0xffff, 0x7fff },  /* Samsung Galaxy Book2 Pro 360 (NP950QED) -> ALC298_FIXUP_SAMSUNG_AMP_V2_2_AMPS (nothing to do) */
  { 0x144d, 0xc1a3, 0xffff, 0x00b7 },  /* Samsung Galaxy Book Pro (NP935XDB-KC1SE) -> ALC298_FIXUP_SAMSUNG_AMP */
  { 0x144d, 0xc1a4, 0xffff, 0x00b7 },  /* Samsung Galaxy Book Pro 360 (NT935QBD) -> ALC298_FIXUP_SAMSUNG_AMP */
  { 0x144d, 0xc1a6, 0xffff, 0x00b7 },  /* Samsung Galaxy Book Pro 360 (NP930QBD) -> ALC298_FIXUP_SAMSUNG_AMP */
  { 0x144d, 0xc740, 0xffff, 0x7fff },  /* Samsung Ativ book 8 (NP870Z5G) -> ALC269_FIXUP_ATIV_BOOK_8 (nothing to do) */
  { 0x144d, 0xc812, 0xffff, 0x00b7 },  /* Samsung Notebook Pen S (NT950SBE-X58) -> ALC298_FIXUP_SAMSUNG_AMP */
  { 0x144d, 0xc824, 0xffff, 0x80b8 },  /* Samsung ELECTRONICS (750XBE/730XBE) [codec SSID] -> ALC298_FIXUP_SAMSUNG_HEADPHONE_VERY_QUIET */
  { 0x144d, 0xc830, 0xffff, 0x00b7 },  /* Samsung Galaxy Book Ion (NT950XCJ-X716A) -> ALC298_FIXUP_SAMSUNG_AMP */
  { 0x144d, 0xc832, 0xffff, 0x00b9 },  /* Samsung Galaxy Book Flex Alpha (NP730QCJ) -> ALC256_FIXUP_SAMSUNG_HEADPHONE_VERY_QUIET */
  { 0x144d, 0xc876, 0xffff, 0x00b9 },  /* Samsung 730QED (NP730QED-KA2US) -> ALC256_FIXUP_SAMSUNG_HEADPHONE_VERY_QUIET */
  { 0x144d, 0xca03, 0xffff, 0x00b7 },  /* Samsung Galaxy Book2 Pro 360 (NP930QED) -> ALC298_FIXUP_SAMSUNG_AMP */
  { 0x144d, 0xca06, 0xffff, 0x00b8 },  /* Samsung Galaxy Book3 360 (NP730QFG) -> ALC298_FIXUP_SAMSUNG_HEADPHONE_VERY_QUIET */
  { 0x144d, 0xc868, 0xffff, 0x00b7 },  /* Samsung Galaxy Book2 Pro (NP930XED) -> ALC298_FIXUP_SAMSUNG_AMP */
  { 0x144d, 0xc870, 0xffff, 0x7fff },  /* Samsung Galaxy Book2 Pro (NP950XED) -> ALC298_FIXUP_SAMSUNG_AMP_V2_2_AMPS (nothing to do) */
  { 0x144d, 0xc872, 0xffff, 0x7fff },  /* Samsung Galaxy Book2 Pro (NP950XEE) -> ALC298_FIXUP_SAMSUNG_AMP_V2_2_AMPS (nothing to do) */
  { 0x144d, 0xc886, 0xffff, 0x7fff },  /* Samsung Galaxy Book3 Pro (NP964XFG) -> ALC298_FIXUP_SAMSUNG_AMP_V2_4_AMPS (nothing to do) */
  { 0x144d, 0xc902, 0xffff, 0x00b9 },  /* Samsung Galaxy Book5 360 (NP750QHA) -> ALC256_FIXUP_SAMSUNG_HEADPHONE_VERY_QUIET */
  { 0x144d, 0xc1ca, 0xffff, 0x7fff },  /* Samsung Galaxy Book3 Pro 360 (NP960QFG) -> ALC298_FIXUP_SAMSUNG_AMP_V2_4_AMPS (nothing to do) */
  { 0x144d, 0xc1cb, 0xffff, 0x7fff },  /* Samsung Galaxy Book3 Pro 360 (NP965QFG) -> ALC298_FIXUP_SAMSUNG_AMP_V2_4_AMPS (nothing to do) */
  { 0x144d, 0xc1cc, 0xffff, 0x7fff },  /* Samsung Galaxy Book3 Ultra (NT960XFH) -> ALC298_FIXUP_SAMSUNG_AMP_V2_4_AMPS (nothing to do) */
  { 0x1458, 0x900e, 0xffff, 0x7fff },  /* Gigabyte G5 KF5 (2023) -> ALC2XX_FIXUP_HEADSET_MIC (nothing to do) */
  { 0x1458, 0xfa53, 0xffff, 0x00ba },  /* Gigabyte BXBT-2807 -> ALC283_FIXUP_HEADSET_MIC */
  { 0x1462, 0xb120, 0xffff, 0x00ba },  /* MSI Cubi MS-B120 -> ALC283_FIXUP_HEADSET_MIC */
  { 0x1462, 0xb171, 0xffff, 0x00ba },  /* Cubi N 8GL (MS-B171) -> ALC283_FIXUP_HEADSET_MIC */
  { 0x152d, 0x1082, 0xffff, 0x000c },  /* Quanta NL3 -> ALC269_FIXUP_LIFEBOOK */
  { 0x152d, 0x1262, 0xffff, 0x7fff },  /* Huawei NBLB-WAX9N -> ALC2XX_FIXUP_HEADSET_MIC (nothing to do) */
  { 0x1558, 0x0353, 0xffff, 0x00bb },  /* Clevo V35[05]SN[CDE]Q -> ALC256_FIXUP_SYSTEM76_MIC_NO_PRESENCE */
  { 0x1558, 0x1323, 0xffff, 0x00bc },  /* Clevo N130ZU -> ALC293_FIXUP_SYSTEM76_MIC_NO_PRESENCE */
  { 0x1558, 0x1325, 0xffff, 0x00bc },  /* Clevo N15[01][CW]U -> ALC293_FIXUP_SYSTEM76_MIC_NO_PRESENCE */
  { 0x1558, 0x1401, 0xffff, 0x00bc },  /* Clevo L140[CZ]U -> ALC293_FIXUP_SYSTEM76_MIC_NO_PRESENCE */
  { 0x1558, 0x1403, 0xffff, 0x00bc },  /* Clevo N140CU -> ALC293_FIXUP_SYSTEM76_MIC_NO_PRESENCE */
  { 0x1558, 0x1404, 0xffff, 0x00bc },  /* Clevo N150CU -> ALC293_FIXUP_SYSTEM76_MIC_NO_PRESENCE */
  { 0x1558, 0x14a1, 0xffff, 0x00bc },  /* Clevo L141MU -> ALC293_FIXUP_SYSTEM76_MIC_NO_PRESENCE */
  { 0x1558, 0x2624, 0xffff, 0x00bb },  /* Clevo L240TU -> ALC256_FIXUP_SYSTEM76_MIC_NO_PRESENCE */
  { 0x1558, 0x28c1, 0xffff, 0x7fff },  /* Clevo V370VND -> ALC2XX_FIXUP_HEADSET_MIC (nothing to do) */
  { 0x1558, 0x35a1, 0xffff, 0x00bb },  /* Clevo V3[56]0EN[CDE] -> ALC256_FIXUP_SYSTEM76_MIC_NO_PRESENCE */
  { 0x1558, 0x35b1, 0xffff, 0x00bb },  /* Clevo V3[57]0WN[MNP]Q -> ALC256_FIXUP_SYSTEM76_MIC_NO_PRESENCE */
  { 0x1558, 0x4018, 0xffff, 0x00bc },  /* Clevo NV40M[BE] -> ALC293_FIXUP_SYSTEM76_MIC_NO_PRESENCE */
  { 0x1558, 0x4019, 0xffff, 0x00bc },  /* Clevo NV40MZ -> ALC293_FIXUP_SYSTEM76_MIC_NO_PRESENCE */
  { 0x1558, 0x4020, 0xffff, 0x00bc },  /* Clevo NV40MB -> ALC293_FIXUP_SYSTEM76_MIC_NO_PRESENCE */
  { 0x1558, 0x4041, 0xffff, 0x00bb },  /* Clevo NV4[15]PZ -> ALC256_FIXUP_SYSTEM76_MIC_NO_PRESENCE */
  { 0x1558, 0x40a1, 0xffff, 0x00bc },  /* Clevo NL40GU -> ALC293_FIXUP_SYSTEM76_MIC_NO_PRESENCE */
  { 0x1558, 0x40c1, 0xffff, 0x00bc },  /* Clevo NL40[CZ]U -> ALC293_FIXUP_SYSTEM76_MIC_NO_PRESENCE */
  { 0x1558, 0x40d1, 0xffff, 0x00bc },  /* Clevo NL41DU -> ALC293_FIXUP_SYSTEM76_MIC_NO_PRESENCE */
  { 0x1558, 0x5015, 0xffff, 0x00bb },  /* Clevo NH5[58]H[HJK]Q -> ALC256_FIXUP_SYSTEM76_MIC_NO_PRESENCE */
  { 0x1558, 0x5017, 0xffff, 0x00bb },  /* Clevo NH7[79]H[HJK]Q -> ALC256_FIXUP_SYSTEM76_MIC_NO_PRESENCE */
  { 0x1558, 0x50a3, 0xffff, 0x00bc },  /* Clevo NJ51GU -> ALC293_FIXUP_SYSTEM76_MIC_NO_PRESENCE */
  { 0x1558, 0x50b3, 0xffff, 0x00bc },  /* Clevo NK50S[BEZ] -> ALC293_FIXUP_SYSTEM76_MIC_NO_PRESENCE */
  { 0x1558, 0x50b6, 0xffff, 0x00bc },  /* Clevo NK50S5 -> ALC293_FIXUP_SYSTEM76_MIC_NO_PRESENCE */
  { 0x1558, 0x50b8, 0xffff, 0x00bc },  /* Clevo NK50SZ -> ALC293_FIXUP_SYSTEM76_MIC_NO_PRESENCE */
  { 0x1558, 0x50d5, 0xffff, 0x00bc },  /* Clevo NP50D5 -> ALC293_FIXUP_SYSTEM76_MIC_NO_PRESENCE */
  { 0x1558, 0x50e1, 0xffff, 0x00bb },  /* Clevo NH5[58]HPQ -> ALC256_FIXUP_SYSTEM76_MIC_NO_PRESENCE */
  { 0x1558, 0x50e2, 0xffff, 0x00bb },  /* Clevo NH7[79]HPQ -> ALC256_FIXUP_SYSTEM76_MIC_NO_PRESENCE */
  { 0x1558, 0x50f0, 0xffff, 0x00bc },  /* Clevo NH50A[CDF] -> ALC293_FIXUP_SYSTEM76_MIC_NO_PRESENCE */
  { 0x1558, 0x50f2, 0xffff, 0x00bc },  /* Clevo NH50E[PR] -> ALC293_FIXUP_SYSTEM76_MIC_NO_PRESENCE */
  { 0x1558, 0x50f3, 0xffff, 0x00bc },  /* Clevo NH58DPQ -> ALC293_FIXUP_SYSTEM76_MIC_NO_PRESENCE */
  { 0x1558, 0x50f5, 0xffff, 0x00bc },  /* Clevo NH55EPY -> ALC293_FIXUP_SYSTEM76_MIC_NO_PRESENCE */
  { 0x1558, 0x50f6, 0xffff, 0x00bc },  /* Clevo NH55DPQ -> ALC293_FIXUP_SYSTEM76_MIC_NO_PRESENCE */
  { 0x1558, 0x5101, 0xffff, 0x00bc },  /* Clevo S510WU -> ALC293_FIXUP_SYSTEM76_MIC_NO_PRESENCE */
  { 0x1558, 0x5157, 0xffff, 0x00bc },  /* Clevo W517GU1 -> ALC293_FIXUP_SYSTEM76_MIC_NO_PRESENCE */
  { 0x1558, 0x51a1, 0xffff, 0x00bc },  /* Clevo NS50MU -> ALC293_FIXUP_SYSTEM76_MIC_NO_PRESENCE */
  { 0x1558, 0x51b1, 0xffff, 0x00bb },  /* Clevo NS50AU -> ALC256_FIXUP_SYSTEM76_MIC_NO_PRESENCE */
  { 0x1558, 0x51b3, 0xffff, 0x00bb },  /* Clevo NS70AU -> ALC256_FIXUP_SYSTEM76_MIC_NO_PRESENCE */
  { 0x1558, 0x5630, 0xffff, 0x00bb },  /* Clevo NP50RNJS -> ALC256_FIXUP_SYSTEM76_MIC_NO_PRESENCE */
  { 0x1558, 0x5700, 0xffff, 0x00bb },  /* Clevo X560WN[RST] -> ALC256_FIXUP_SYSTEM76_MIC_NO_PRESENCE */
  { 0x1558, 0x6480, 0xffff, 0x00bd },  /* Clevo V6xxAW -> ALC245_FIXUP_CLEVO_NOISY_MIC */
  { 0x1558, 0x70a1, 0xffff, 0x00bc },  /* Clevo NB70T[HJK] -> ALC293_FIXUP_SYSTEM76_MIC_NO_PRESENCE */
  { 0x1558, 0x70b3, 0xffff, 0x00bc },  /* Clevo NK70SB -> ALC293_FIXUP_SYSTEM76_MIC_NO_PRESENCE */
  { 0x1558, 0x70f2, 0xffff, 0x00bc },  /* Clevo NH79EPY -> ALC293_FIXUP_SYSTEM76_MIC_NO_PRESENCE */
  { 0x1558, 0x70f3, 0xffff, 0x00bc },  /* Clevo NH77DPQ -> ALC293_FIXUP_SYSTEM76_MIC_NO_PRESENCE */
  { 0x1558, 0x70f4, 0xffff, 0x00bc },  /* Clevo NH77EPY -> ALC293_FIXUP_SYSTEM76_MIC_NO_PRESENCE */
  { 0x1558, 0x70f6, 0xffff, 0x00bc },  /* Clevo NH77DPQ-Y -> ALC293_FIXUP_SYSTEM76_MIC_NO_PRESENCE */
  { 0x1558, 0x7716, 0xffff, 0x00bb },  /* Clevo NS50PU -> ALC256_FIXUP_SYSTEM76_MIC_NO_PRESENCE */
  { 0x1558, 0x7717, 0xffff, 0x00bb },  /* Clevo NS70PU -> ALC256_FIXUP_SYSTEM76_MIC_NO_PRESENCE */
  { 0x1558, 0x7718, 0xffff, 0x00bb },  /* Clevo L140PU -> ALC256_FIXUP_SYSTEM76_MIC_NO_PRESENCE */
  { 0x1558, 0x7724, 0xffff, 0x00bb },  /* Clevo L140AU -> ALC256_FIXUP_SYSTEM76_MIC_NO_PRESENCE */
  { 0x1558, 0x8228, 0xffff, 0x00bc },  /* Clevo NR40BU -> ALC293_FIXUP_SYSTEM76_MIC_NO_PRESENCE */
  { 0x1558, 0x8520, 0xffff, 0x00bc },  /* Clevo NH50D[CD] -> ALC293_FIXUP_SYSTEM76_MIC_NO_PRESENCE */
  { 0x1558, 0x8521, 0xffff, 0x00bc },  /* Clevo NH77D[CD] -> ALC293_FIXUP_SYSTEM76_MIC_NO_PRESENCE */
  { 0x1558, 0x8535, 0xffff, 0x00bc },  /* Clevo NH50D[BE] -> ALC293_FIXUP_SYSTEM76_MIC_NO_PRESENCE */
  { 0x1558, 0x8536, 0xffff, 0x00bc },  /* Clevo NH79D[BE] -> ALC293_FIXUP_SYSTEM76_MIC_NO_PRESENCE */
  { 0x1558, 0x8550, 0xffff, 0x00bc },  /* Clevo NH[57][0-9][ER][ACDH]Q -> ALC293_FIXUP_SYSTEM76_MIC_NO_PRESENCE */
  { 0x1558, 0x8551, 0xffff, 0x00bc },  /* Clevo NH[57][0-9][ER][ACDH]Q -> ALC293_FIXUP_SYSTEM76_MIC_NO_PRESENCE */
  { 0x1558, 0x8560, 0xffff, 0x7fff },  /* Clevo NH[57][0-9][ER][ACDH]Q -> ALC269_FIXUP_HEADSET_MIC (nothing to do) */
  { 0x1558, 0x8561, 0xffff, 0x7fff },  /* Clevo NH[57][0-9][ER][ACDH]Q -> ALC269_FIXUP_HEADSET_MIC (nothing to do) */
  { 0x1558, 0x8562, 0xffff, 0x00be },  /* Clevo NH[57][0-9]RZ[Q] -> ALC269_FIXUP_DMIC */
  { 0x1558, 0x8668, 0xffff, 0x00bc },  /* Clevo NP50B[BE] -> ALC293_FIXUP_SYSTEM76_MIC_NO_PRESENCE */
  { 0x1558, 0x866d, 0xffff, 0x00bb },  /* Clevo NP5[05]PN[HJK] -> ALC256_FIXUP_SYSTEM76_MIC_NO_PRESENCE */
  { 0x1558, 0x867c, 0xffff, 0x00bb },  /* Clevo NP7[01]PNP -> ALC256_FIXUP_SYSTEM76_MIC_NO_PRESENCE */
  { 0x1558, 0x867d, 0xffff, 0x00bb },  /* Clevo NP7[01]PN[HJK] -> ALC256_FIXUP_SYSTEM76_MIC_NO_PRESENCE */
  { 0x1558, 0x8680, 0xffff, 0x00bc },  /* Clevo NJ50LU -> ALC293_FIXUP_SYSTEM76_MIC_NO_PRESENCE */
  { 0x1558, 0x8686, 0xffff, 0x7fff },  /* Clevo NH50[CZ]U -> ALC256_FIXUP_MIC_NO_PRESENCE_AND_RESUME (nothing to do) */
  { 0x1558, 0x8a20, 0xffff, 0x00bc },  /* Clevo NH55DCQ-Y -> ALC293_FIXUP_SYSTEM76_MIC_NO_PRESENCE */
  { 0x1558, 0x8a51, 0xffff, 0x00bc },  /* Clevo NH70RCQ-Y -> ALC293_FIXUP_SYSTEM76_MIC_NO_PRESENCE */
  { 0x1558, 0x8d50, 0xffff, 0x00bc },  /* Clevo NH55RCQ-M -> ALC293_FIXUP_SYSTEM76_MIC_NO_PRESENCE */
  { 0x1558, 0x951d, 0xffff, 0x00bc },  /* Clevo N950T[CDF] -> ALC293_FIXUP_SYSTEM76_MIC_NO_PRESENCE */
  { 0x1558, 0x9600, 0xffff, 0x00bc },  /* Clevo N960K[PR] -> ALC293_FIXUP_SYSTEM76_MIC_NO_PRESENCE */
  { 0x1558, 0x961d, 0xffff, 0x00bc },  /* Clevo N960S[CDF] -> ALC293_FIXUP_SYSTEM76_MIC_NO_PRESENCE */
  { 0x1558, 0x971d, 0xffff, 0x00bc },  /* Clevo N970T[CDF] -> ALC293_FIXUP_SYSTEM76_MIC_NO_PRESENCE */
  { 0x1558, 0xa500, 0xffff, 0x00bc },  /* Clevo NL5[03]RU -> ALC293_FIXUP_SYSTEM76_MIC_NO_PRESENCE */
  { 0x1558, 0xa554, 0xffff, 0x00bf },  /* VAIO VJFH52 -> ALC269_FIXUP_VAIO_VJFH52_MIC_NO_PRESENCE */
  { 0x1558, 0xa559, 0xffff, 0x00c1 },  /* VAIO RPL -> ALC256_FIXUP_VAIO_RPL_MIC_NO_PRESENCE */
  { 0x1558, 0xa600, 0xffff, 0x00bc },  /* Clevo NL50NU -> ALC293_FIXUP_SYSTEM76_MIC_NO_PRESENCE */
  { 0x1558, 0xa650, 0xffff, 0x00bb },  /* Clevo NP[567]0SN[CD] -> ALC256_FIXUP_SYSTEM76_MIC_NO_PRESENCE */
  { 0x1558, 0xa671, 0xffff, 0x00bb },  /* Clevo NP70SN[CDE] -> ALC256_FIXUP_SYSTEM76_MIC_NO_PRESENCE */
  { 0x1558, 0xa741, 0xffff, 0x00bd },  /* Clevo V54x_6x_TNE -> ALC245_FIXUP_CLEVO_NOISY_MIC */
  { 0x1558, 0xa743, 0xffff, 0x00bd },  /* Clevo V54x_6x_TU -> ALC245_FIXUP_CLEVO_NOISY_MIC */
  { 0x1558, 0xa763, 0xffff, 0x00bd },  /* Clevo V54x_6x_TU -> ALC245_FIXUP_CLEVO_NOISY_MIC */
  { 0x1558, 0xb018, 0xffff, 0x00bc },  /* Clevo NP50D[BE] -> ALC293_FIXUP_SYSTEM76_MIC_NO_PRESENCE */
  { 0x1558, 0xb019, 0xffff, 0x00bc },  /* Clevo NH77D[BE]Q -> ALC293_FIXUP_SYSTEM76_MIC_NO_PRESENCE */
  { 0x1558, 0xb022, 0xffff, 0x00bc },  /* Clevo NH77D[DC][QW] -> ALC293_FIXUP_SYSTEM76_MIC_NO_PRESENCE */
  { 0x1558, 0xc018, 0xffff, 0x00bc },  /* Clevo NP50D[BE] -> ALC293_FIXUP_SYSTEM76_MIC_NO_PRESENCE */
  { 0x1558, 0xc019, 0xffff, 0x00bc },  /* Clevo NH77D[BE]Q -> ALC293_FIXUP_SYSTEM76_MIC_NO_PRESENCE */
  { 0x1558, 0xc022, 0xffff, 0x00bc },  /* Clevo NH77[DC][QW] -> ALC293_FIXUP_SYSTEM76_MIC_NO_PRESENCE */
  { 0x17aa, 0x1036, 0xffff, 0x00c2 },  /* Lenovo P520 -> ALC233_FIXUP_LENOVO_MULTI_CODECS */
  { 0x17aa, 0x1048, 0xffff, 0x00c4 },  /* ThinkCentre Station -> ALC623_FIXUP_LENOVO_THINKSTATION_P340 */
  { 0x17aa, 0x20f2, 0xffff, 0x0076 },  /* Thinkpad SL410/510 -> ALC269_FIXUP_SKU_IGNORE */
  { 0x17aa, 0x215e, 0xffff, 0x0076 },  /* Thinkpad L512 -> ALC269_FIXUP_SKU_IGNORE */
  { 0x17aa, 0x21b8, 0xffff, 0x0076 },  /* Thinkpad Edge 14 -> ALC269_FIXUP_SKU_IGNORE */
  { 0x17aa, 0x21ca, 0xffff, 0x0076 },  /* Thinkpad L412 -> ALC269_FIXUP_SKU_IGNORE */
  { 0x17aa, 0x21e9, 0xffff, 0x0076 },  /* Thinkpad Edge 15 -> ALC269_FIXUP_SKU_IGNORE */
  { 0x17aa, 0x21f3, 0xffff, 0x00c5 },  /* Thinkpad T430 -> ALC269_FIXUP_LENOVO_DOCK */
  { 0x17aa, 0x21f6, 0xffff, 0x00c7 },  /* Thinkpad T530 -> ALC269_FIXUP_LENOVO_DOCK_LIMIT_BOOST */
  { 0x17aa, 0x21fa, 0xffff, 0x00c5 },  /* Thinkpad X230 -> ALC269_FIXUP_LENOVO_DOCK */
  { 0x17aa, 0x21fb, 0xffff, 0x00c5 },  /* Thinkpad T430s -> ALC269_FIXUP_LENOVO_DOCK */
  { 0x17aa, 0x2203, 0xffff, 0x00c5 },  /* Thinkpad X230 Tablet -> ALC269_FIXUP_LENOVO_DOCK */
  { 0x17aa, 0x2208, 0xffff, 0x00c5 },  /* Thinkpad T431s -> ALC269_FIXUP_LENOVO_DOCK */
  { 0x17aa, 0x220c, 0xffff, 0x7fff },  /* Thinkpad T440s -> ALC292_FIXUP_TPT440 (nothing to do) */
  { 0x17aa, 0x220e, 0xffff, 0x7fff },  /* Thinkpad T440p -> ALC292_FIXUP_TPT440_DOCK (nothing to do) */
  { 0x17aa, 0x2210, 0xffff, 0x7fff },  /* Thinkpad T540p -> ALC292_FIXUP_TPT440_DOCK (nothing to do) */
  { 0x17aa, 0x2211, 0xffff, 0x7fff },  /* Thinkpad W541 -> ALC292_FIXUP_TPT440_DOCK (nothing to do) */
  { 0x17aa, 0x2212, 0xffff, 0x7fff },  /* Thinkpad T440 -> ALC292_FIXUP_TPT440_DOCK (nothing to do) */
  { 0x17aa, 0x2214, 0xffff, 0x7fff },  /* Thinkpad X240 -> ALC292_FIXUP_TPT440_DOCK (nothing to do) */
  { 0x17aa, 0x2215, 0xffff, 0x00c8 },  /* Thinkpad -> ALC269_FIXUP_THINKPAD_LIMIT_INT_MIC_BOOST */
  { 0x17aa, 0x2218, 0xffff, 0x7fff },  /* Thinkpad X1 Carbon 2nd -> ALC292_FIXUP_TPT440_DOCK (nothing to do) */
  { 0x17aa, 0x2223, 0xffff, 0x7fff },  /* ThinkPad T550 -> ALC292_FIXUP_TPT440_DOCK (nothing to do) */
  { 0x17aa, 0x2226, 0xffff, 0x7fff },  /* ThinkPad X250 -> ALC292_FIXUP_TPT440_DOCK (nothing to do) */
  { 0x17aa, 0x222d, 0xffff, 0x00c9 },  /* Thinkpad -> ALC298_FIXUP_TPT470_DOCK */
  { 0x17aa, 0x222e, 0xffff, 0x00c9 },  /* Thinkpad -> ALC298_FIXUP_TPT470_DOCK */
  { 0x17aa, 0x2231, 0xffff, 0x00cc },  /* Thinkpad T560 -> ALC292_FIXUP_TPT460 */
  { 0x17aa, 0x2233, 0xffff, 0x00cc },  /* Thinkpad -> ALC292_FIXUP_TPT460 */
  { 0x17aa, 0x2234, 0xffff, 0x0072 },  /* Thinkpad ICE-1 -> ALC287_FIXUP_TAS2781_I2C */
  { 0x17aa, 0x2245, 0xffff, 0x00c9 },  /* Thinkpad T470 -> ALC298_FIXUP_TPT470_DOCK */
  { 0x17aa, 0x2246, 0xffff, 0x00c9 },  /* Thinkpad -> ALC298_FIXUP_TPT470_DOCK */
  { 0x17aa, 0x2247, 0xffff, 0x00c9 },  /* Thinkpad -> ALC298_FIXUP_TPT470_DOCK */
  { 0x17aa, 0x2249, 0xffff, 0x00cc },  /* Thinkpad -> ALC292_FIXUP_TPT460 */
  { 0x17aa, 0x224b, 0xffff, 0x00c9 },  /* Thinkpad -> ALC298_FIXUP_TPT470_DOCK */
  { 0x17aa, 0x224c, 0xffff, 0x00c9 },  /* Thinkpad -> ALC298_FIXUP_TPT470_DOCK */
  { 0x17aa, 0x224d, 0xffff, 0x00c9 },  /* Thinkpad -> ALC298_FIXUP_TPT470_DOCK */
  { 0x17aa, 0x225c, 0xffff, 0x7fff },  /* Lenovo ThinkPad X1 Carbon 6th Gen -> ALC285_LENOVO_DAC_RENAME (nothing to do) */
  { 0x17aa, 0x225d, 0xffff, 0x00c8 },  /* Thinkpad T480 -> ALC269_FIXUP_THINKPAD_LIMIT_INT_MIC_BOOST */
  { 0x17aa, 0x2288, 0xffff, 0x00cd },  /* Thinkpad X390 -> ALC285_FIXUP_THINKPAD_NO_BASS_SPK_HEADSET_JACK */
  { 0x17aa, 0x2292, 0xffff, 0x0073 },  /* Thinkpad X1 Carbon 7th -> ALC285_FIXUP_THINKPAD_HEADSET_JACK */
  { 0x17aa, 0x22be, 0xffff, 0x0073 },  /* Thinkpad X1 Carbon 8th -> ALC285_FIXUP_THINKPAD_HEADSET_JACK */
  { 0x17aa, 0x22c1, 0xffff, 0x00cd },  /* Thinkpad P1 Gen 3 -> ALC285_FIXUP_THINKPAD_NO_BASS_SPK_HEADSET_JACK */
  { 0x17aa, 0x22c2, 0xffff, 0x00cd },  /* Thinkpad X1 Extreme Gen 3 -> ALC285_FIXUP_THINKPAD_NO_BASS_SPK_HEADSET_JACK */
  { 0x17aa, 0x22f1, 0xffff, 0x00ce },  /* Thinkpad -> ALC287_FIXUP_MG_RTKC_CSAMP_CS35L41_I2C_THINKPAD */
  { 0x17aa, 0x22f2, 0xffff, 0x00ce },  /* Thinkpad -> ALC287_FIXUP_MG_RTKC_CSAMP_CS35L41_I2C_THINKPAD */
  { 0x17aa, 0x22f3, 0xffff, 0x00ce },  /* Thinkpad -> ALC287_FIXUP_MG_RTKC_CSAMP_CS35L41_I2C_THINKPAD */
  { 0x17aa, 0x2316, 0xffff, 0x00ce },  /* Thinkpad P1 Gen 6 -> ALC287_FIXUP_MG_RTKC_CSAMP_CS35L41_I2C_THINKPAD */
  { 0x17aa, 0x2317, 0xffff, 0x00ce },  /* Thinkpad P1 Gen 6 -> ALC287_FIXUP_MG_RTKC_CSAMP_CS35L41_I2C_THINKPAD */
  { 0x17aa, 0x2318, 0xffff, 0x00ce },  /* Thinkpad Z13 Gen2 -> ALC287_FIXUP_MG_RTKC_CSAMP_CS35L41_I2C_THINKPAD */
  { 0x17aa, 0x2319, 0xffff, 0x00ce },  /* Thinkpad Z16 Gen2 -> ALC287_FIXUP_MG_RTKC_CSAMP_CS35L41_I2C_THINKPAD */
  { 0x17aa, 0x231a, 0xffff, 0x00ce },  /* Thinkpad Z16 Gen2 -> ALC287_FIXUP_MG_RTKC_CSAMP_CS35L41_I2C_THINKPAD */
  { 0x17aa, 0x231e, 0xffff, 0x00d0 },  /* Thinkpad -> ALC287_FIXUP_LENOVO_THKPAD_WH_ALC1318 */
  { 0x17aa, 0x231f, 0xffff, 0x00d0 },  /* Thinkpad -> ALC287_FIXUP_LENOVO_THKPAD_WH_ALC1318 */
  { 0x17aa, 0x2326, 0xffff, 0x0072 },  /* Hera2 -> ALC287_FIXUP_TAS2781_I2C */
  { 0x17aa, 0x30bb, 0xffff, 0x7fff },  /* ThinkCentre AIO -> ALC233_FIXUP_LENOVO_LINE2_MIC_HOTKEY (nothing to do) */
  { 0x17aa, 0x30e2, 0xffff, 0x7fff },  /* ThinkCentre AIO -> ALC233_FIXUP_LENOVO_LINE2_MIC_HOTKEY (nothing to do) */
  { 0x17aa, 0x310c, 0xffff, 0x009c },  /* ThinkCentre Station -> ALC294_FIXUP_LENOVO_MIC_LOCATION */
  { 0x17aa, 0x3111, 0xffff, 0x009c },  /* ThinkCentre Station -> ALC294_FIXUP_LENOVO_MIC_LOCATION */
  { 0x17aa, 0x312a, 0xffff, 0x009c },  /* ThinkCentre Station -> ALC294_FIXUP_LENOVO_MIC_LOCATION */
  { 0x17aa, 0x312f, 0xffff, 0x009c },  /* ThinkCentre Station -> ALC294_FIXUP_LENOVO_MIC_LOCATION */
  { 0x17aa, 0x313c, 0xffff, 0x009c },  /* ThinkCentre Station -> ALC294_FIXUP_LENOVO_MIC_LOCATION */
  { 0x17aa, 0x3151, 0xffff, 0x00ba },  /* ThinkCentre Station -> ALC283_FIXUP_HEADSET_MIC */
  { 0x17aa, 0x3176, 0xffff, 0x00ba },  /* ThinkCentre Station -> ALC283_FIXUP_HEADSET_MIC */
  { 0x17aa, 0x3178, 0xffff, 0x00ba },  /* ThinkCentre Station -> ALC283_FIXUP_HEADSET_MIC */
  { 0x17aa, 0x31af, 0xffff, 0x00c4 },  /* ThinkCentre Station -> ALC623_FIXUP_LENOVO_THINKSTATION_P340 */
  { 0x17aa, 0x3341, 0xffff, 0x7fff },  /* Lenovo ThinkCentre M90 Gen4 -> ALC233_FIXUP_LENOVO_GPIO2_MIC_HOTKEY (nothing to do) */
  { 0x17aa, 0x3342, 0xffff, 0x7fff },  /* Lenovo ThinkCentre M90 Gen4 -> ALC233_FIXUP_LENOVO_GPIO2_MIC_HOTKEY (nothing to do) */
  { 0x17aa, 0x3343, 0xffff, 0x7fff },  /* Lenovo ThinkCentre M70 Gen4 -> ALC233_FIXUP_LENOVO_GPIO2_MIC_HOTKEY (nothing to do) */
  { 0x17aa, 0x3344, 0xffff, 0x7fff },  /* Lenovo ThinkCentre M70 Gen4 -> ALC233_FIXUP_LENOVO_GPIO2_MIC_HOTKEY (nothing to do) */
  { 0x17aa, 0x334b, 0xffff, 0x00ba },  /* Lenovo ThinkCentre M70 Gen5 -> ALC283_FIXUP_HEADSET_MIC */
  { 0x17aa, 0x334f, 0xffff, 0x7fff },  /* Lenovo ThinkCentre M90a Gen5 -> ALC233_FIXUP_LENOVO_GPIO2_MIC_HOTKEY (nothing to do) */
  { 0x17aa, 0x3384, 0xffff, 0x7fff },  /* ThinkCentre M90a PRO -> ALC233_FIXUP_LENOVO_L2MH_LOW_ENLED (nothing to do) */
  { 0x17aa, 0x3386, 0xffff, 0x7fff },  /* ThinkCentre M90a Gen6 -> ALC233_FIXUP_LENOVO_L2MH_LOW_ENLED (nothing to do) */
  { 0x17aa, 0x3387, 0xffff, 0x7fff },  /* ThinkCentre M70a Gen6 -> ALC233_FIXUP_LENOVO_L2MH_LOW_ENLED (nothing to do) */
  { 0x17aa, 0x3801, 0xffff, 0x00d1 },  /* Lenovo Yoga9 14IAP7 -> ALC287_FIXUP_YOGA9_14IAP7_BASS_SPK_PIN */
  { 0x17aa, 0x3802, 0xffff, 0x80d3 },  /* DuetITL 2021 [codec SSID] -> ALC287_FIXUP_YOGA7_14ITL_SPEAKERS */
  { 0x17aa, 0x3802, 0xffff, 0x0072 },  /* Lenovo Yoga Pro 9 14IRP8 -> ALC287_FIXUP_TAS2781_I2C */
  { 0x17aa, 0x380b, 0xffff, 0x00d1 },  /* Lenovo Yoga Slim 9 14ILL10 -> ALC287_FIXUP_YOGA9_14IAP7_BASS_SPK_PIN */
  { 0x17aa, 0x38d5, 0xffff, 0x8072 },  /* Lenovo Yoga Pro 9 16IMH9 [codec SSID] -> ALC287_FIXUP_TAS2781_I2C */
  { 0x17aa, 0x38d6, 0xffff, 0x8072 },  /* Lenovo Yoga Pro 9 16IMH9 [codec SSID] -> ALC287_FIXUP_TAS2781_I2C */
  { 0x17aa, 0x3855, 0xffff, 0xffff },  /* Legion 7 16ITHG6 [codec SSID] -> ALC287_FIXUP_LEGION_16ITHG6 (nothing to do) */
  { 0x17aa, 0x3811, 0xffff, 0x00d4 },  /* Legion S7 15IMH05 -> ALC287_FIXUP_LEGION_15IMHG05_SPEAKERS */
  { 0x17aa, 0x3813, 0xffff, 0x00d4 },  /* Legion 7i 15IMHG05 -> ALC287_FIXUP_LEGION_15IMHG05_SPEAKERS */
  { 0x17aa, 0x3818, 0xffff, 0x7fff },  /* Lenovo C940 / Yoga Duet 7 -> ALC298_FIXUP_LENOVO_C940_DUET7 (nothing to do) */
  { 0x17aa, 0x3819, 0xffff, 0x00d6 },  /* Lenovo 13s Gen2 ITL -> ALC287_FIXUP_13S_GEN2_SPEAKERS */
  { 0x17aa, 0x3820, 0xffff, 0x8002 },  /* IdeaPad 330-17IKB 81DM [codec SSID] -> ALC269_FIXUP_ASPIRE_HEADSET_MIC */
  { 0x17aa, 0x3820, 0xffff, 0x00d3 },  /* Yoga Duet 7 13ITL6 -> ALC287_FIXUP_YOGA7_14ITL_SPEAKERS */
  { 0x17aa, 0x3824, 0xffff, 0x00d7 },  /* Legion Y9000X 2020 -> ALC285_FIXUP_LEGION_Y9000X_SPEAKERS */
  { 0x17aa, 0x3827, 0xffff, 0x00d9 },  /* Ideapad S740 -> ALC285_FIXUP_IDEAPAD_S740_COEF */
  { 0x17aa, 0x3834, 0xffff, 0x00d3 },  /* Lenovo IdeaPad Slim 9i 14ITL5 -> ALC287_FIXUP_YOGA7_14ITL_SPEAKERS */
  { 0x17aa, 0x383d, 0xffff, 0x00d7 },  /* Legion Y9000X 2019 -> ALC285_FIXUP_LEGION_Y9000X_SPEAKERS */
  { 0x17aa, 0x3843, 0xffff, 0x7fff },  /* Lenovo Yoga 9i / Yoga Book 9i -> ALC287_FIXUP_LENOVO_YOGA_BOOK_9I (nothing to do) */
  { 0x17aa, 0x38cf, 0xffff, 0x80da },  /* Lenovo Yoga Pro 7 14IMH9 [codec SSID] -> ALC287_FIXUP_YOGA9_14IMH9_BASS_SPK_PIN */
  { 0x17aa, 0x3847, 0xffff, 0x7fff },  /* Legion 7 16ACHG6 -> ALC287_FIXUP_LEGION_16ACHG6 (nothing to do) */
  { 0x17aa, 0x384a, 0xffff, 0x00d3 },  /* Lenovo Yoga 7 15ITL5 -> ALC287_FIXUP_YOGA7_14ITL_SPEAKERS */
  { 0x17aa, 0x38b1, 0xffff, 0x80d1 },  /* Lenovo Yoga Pro 7 14IRH8 [codec SSID] -> ALC287_FIXUP_YOGA9_14IAP7_BASS_SPK_PIN */
  { 0x17aa, 0x3852, 0xffff, 0x00d3 },  /* Lenovo Yoga 7 14ITL5 -> ALC287_FIXUP_YOGA7_14ITL_SPEAKERS */
  { 0x17aa, 0x3853, 0xffff, 0x00d3 },  /* Lenovo Yoga 7 15ITL5 -> ALC287_FIXUP_YOGA7_14ITL_SPEAKERS */
  { 0x17aa, 0x3855, 0xffff, 0x7fff },  /* Legion 7 16ITHG6 -> ALC287_FIXUP_LEGION_16ITHG6 (nothing to do) */
  { 0x17aa, 0x3862, 0xffff, 0x7fff },  /* Lenovo IdeaPad Slim 3 15ABR8 -> ALC269_FIXUP_LIMIT_INT_MIC_BOOST (nothing to do) */
  { 0x17aa, 0x3865, 0xffff, 0x7fff },  /* Lenovo 13X -> ALC287_FIXUP_CS35L41_I2C_2 (nothing to do) */
  { 0x17aa, 0x3866, 0xffff, 0x7fff },  /* Lenovo 13X -> ALC287_FIXUP_CS35L41_I2C_2 (nothing to do) */
  { 0x17aa, 0x3869, 0xffff, 0x00d1 },  /* Lenovo Yoga7 14IAL7 -> ALC287_FIXUP_YOGA9_14IAP7_BASS_SPK_PIN */
  { 0x17aa, 0x386a, 0xffff, 0x80d1 },  /* Lenovo Yoga 7 16IAP7 [codec SSID] -> ALC287_FIXUP_YOGA9_14IAP7_BASS_SPK_PIN */
  { 0x17aa, 0x386e, 0xffff, 0xffff },  /* Legion Y9000X 2022 IAH7 [codec SSID] -> ALC287_FIXUP_CS35L41_I2C_2 (nothing to do) */
  { 0x17aa, 0x386e, 0xffff, 0x00db },  /* Yoga Pro 7 14ARP8 -> ALC285_FIXUP_YOGA_SPEAKER2_TO_DAC1 */
  { 0x17aa, 0x38a7, 0xffff, 0x8072 },  /* Legion Pro 7 16ARX8H [codec SSID] -> ALC287_FIXUP_TAS2781_I2C */
  { 0x17aa, 0x38a8, 0xffff, 0x8072 },  /* Legion Pro 7 16ARX8H [codec SSID] -> ALC287_FIXUP_TAS2781_I2C */
  { 0x17aa, 0x386f, 0xffff, 0x7fff },  /* Legion Pro 7i 16IAX7 -> ALC287_FIXUP_CS35L41_I2C_2 (nothing to do) */
  { 0x17aa, 0x3870, 0xffff, 0x00dc },  /* Lenovo Yoga 7 14ARB7 -> ALC287_FIXUP_YOGA7_14ARB7_I2C */
  { 0x17aa, 0x3871, 0xffff, 0x00dc },  /* Lenovo ThinkBook 16p Gen 3 ARH -> ALC287_FIXUP_YOGA7_14ARB7_I2C */
  { 0x17aa, 0x3874, 0xffff, 0x7fff },  /* Legion 7i 16IAX7 -> ALC287_FIXUP_CS35L41_I2C_2 (nothing to do) */
  { 0x17aa, 0x3877, 0xffff, 0x7fff },  /* Lenovo Legion 7 Slim 16ARHA7 -> ALC287_FIXUP_CS35L41_I2C_2 (nothing to do) */
  { 0x17aa, 0x3878, 0xffff, 0x7fff },  /* Lenovo Legion 7 Slim 16ARHA7 -> ALC287_FIXUP_CS35L41_I2C_2 (nothing to do) */
  { 0x17aa, 0x387d, 0xffff, 0x0072 },  /* Yoga S780-16 pro Quad AAC -> ALC287_FIXUP_TAS2781_I2C */
  { 0x17aa, 0x387e, 0xffff, 0x0072 },  /* Yoga S780-16 pro Quad YC -> ALC287_FIXUP_TAS2781_I2C */
  { 0x17aa, 0x387f, 0xffff, 0x0072 },  /* Yoga S780-16 pro dual LX -> ALC287_FIXUP_TAS2781_I2C */
  { 0x17aa, 0x3880, 0xffff, 0x0072 },  /* Yoga S780-16 pro dual YC -> ALC287_FIXUP_TAS2781_I2C */
  { 0x17aa, 0x3881, 0xffff, 0x0072 },  /* YB9 dual power mode2 YC -> ALC287_FIXUP_TAS2781_I2C */
  { 0x17aa, 0x3882, 0xffff, 0x00d1 },  /* Lenovo Yoga Pro 7 14APH8 -> ALC287_FIXUP_YOGA9_14IAP7_BASS_SPK_PIN */
  { 0x17aa, 0x3884, 0xffff, 0x0072 },  /* Y780 YG DUAL -> ALC287_FIXUP_TAS2781_I2C */
  { 0x17aa, 0x3886, 0xffff, 0x0072 },  /* Y780 VECO DUAL -> ALC287_FIXUP_TAS2781_I2C */
  { 0x17aa, 0x3891, 0xffff, 0x00d1 },  /* Lenovo Yoga Pro 7 14AHP9 -> ALC287_FIXUP_YOGA9_14IAP7_BASS_SPK_PIN */
  { 0x17aa, 0x38a5, 0xffff, 0x0072 },  /* Y580P AMD dual -> ALC287_FIXUP_TAS2781_I2C */
  { 0x17aa, 0x38a7, 0xffff, 0x0072 },  /* Y780P AMD YG dual -> ALC287_FIXUP_TAS2781_I2C */
  { 0x17aa, 0x38a8, 0xffff, 0x0072 },  /* Y780P AMD VECO dual -> ALC287_FIXUP_TAS2781_I2C */
  { 0x17aa, 0x38a9, 0xffff, 0x00ce },  /* Thinkbook 16P -> ALC287_FIXUP_MG_RTKC_CSAMP_CS35L41_I2C_THINKPAD */
  { 0x17aa, 0x38ab, 0xffff, 0x00ce },  /* Thinkbook 16P -> ALC287_FIXUP_MG_RTKC_CSAMP_CS35L41_I2C_THINKPAD */
  { 0x17aa, 0x391a, 0xffff, 0x80d1 },  /* Lenovo Yoga Slim 7 14AKP10 [codec SSID] -> ALC287_FIXUP_YOGA9_14IAP7_BASS_SPK_PIN */
  { 0x17aa, 0x38b4, 0xffff, 0x7fff },  /* Legion Slim 7 16IRH8 -> ALC287_FIXUP_CS35L41_I2C_2 (nothing to do) */
  { 0x17aa, 0x391c, 0xffff, 0x80d1 },  /* Lenovo Yoga 7 2-in-1 14AKP10 [codec SSID] -> ALC287_FIXUP_YOGA9_14IAP7_BASS_SPK_PIN */
  { 0x17aa, 0x391d, 0xffff, 0x80d1 },  /* Lenovo Yoga 7 2-in-1 16AKP10 [codec SSID] -> ALC287_FIXUP_YOGA9_14IAP7_BASS_SPK_PIN */
  { 0x17aa, 0x38b5, 0xffff, 0x7fff },  /* Legion Slim 7 16IRH8 -> ALC287_FIXUP_CS35L41_I2C_2 (nothing to do) */
  { 0x17aa, 0x38b6, 0xffff, 0x7fff },  /* Legion Slim 7 16APH8 -> ALC287_FIXUP_CS35L41_I2C_2 (nothing to do) */
  { 0x17aa, 0x38b7, 0xffff, 0x7fff },  /* Legion Slim 7 16APH8 -> ALC287_FIXUP_CS35L41_I2C_2 (nothing to do) */
  { 0x17aa, 0x38b8, 0xffff, 0x0072 },  /* Yoga S780-14.5 proX AMD YC Dual -> ALC287_FIXUP_TAS2781_I2C */
  { 0x17aa, 0x38b9, 0xffff, 0x0072 },  /* Yoga S780-14.5 proX AMD LX Dual -> ALC287_FIXUP_TAS2781_I2C */
  { 0x17aa, 0x38ba, 0xffff, 0x0072 },  /* Yoga S780-14.5 Air AMD quad YC -> ALC287_FIXUP_TAS2781_I2C */
  { 0x17aa, 0x3926, 0xffff, 0x80d1 },  /* Legion Pro 5 16ADR10 [codec SSID] -> ALC287_FIXUP_YOGA9_14IAP7_BASS_SPK_PIN */
  { 0x17aa, 0x3927, 0xffff, 0xffff },  /* Legion R9000P ADR10 [codec SSID] -> ALC287_FIXUP_LENOVO_LEGION_AW88399 (nothing to do) */
  { 0x17aa, 0x3928, 0xffff, 0xffff },  /* Legion R9000P ADR10 [codec SSID] -> ALC287_FIXUP_LENOVO_LEGION_AW88399 (nothing to do) */
  { 0x17aa, 0x38bb, 0xffff, 0x0072 },  /* Yoga S780-14.5 Air AMD quad AAC -> ALC287_FIXUP_TAS2781_I2C */
  { 0x17aa, 0x38be, 0xffff, 0x0072 },  /* Yoga S980-14.5 proX YC Dual -> ALC287_FIXUP_TAS2781_I2C */
  { 0x17aa, 0x38bf, 0xffff, 0x0072 },  /* Yoga S980-14.5 proX LX Dual -> ALC287_FIXUP_TAS2781_I2C */
  { 0x17aa, 0x38c3, 0xffff, 0x0072 },  /* Y980 DUAL -> ALC287_FIXUP_TAS2781_I2C */
  { 0x17aa, 0x38c7, 0xffff, 0x00ce },  /* Thinkbook 13x Gen 4 -> ALC287_FIXUP_MG_RTKC_CSAMP_CS35L41_I2C_THINKPAD */
  { 0x17aa, 0x38c8, 0xffff, 0x00ce },  /* Thinkbook 13x Gen 4 -> ALC287_FIXUP_MG_RTKC_CSAMP_CS35L41_I2C_THINKPAD */
  { 0x17aa, 0x38cb, 0xffff, 0x0072 },  /* Y790 YG DUAL -> ALC287_FIXUP_TAS2781_I2C */
  { 0x17aa, 0x38cd, 0xffff, 0x0072 },  /* Y790 VECO DUAL -> ALC287_FIXUP_TAS2781_I2C */
  { 0x17aa, 0x38d2, 0xffff, 0x00da },  /* Lenovo Yoga 9 14IMH9 -> ALC287_FIXUP_YOGA9_14IMH9_BASS_SPK_PIN */
  { 0x17aa, 0x38d3, 0xffff, 0x0072 },  /* Yoga S990-16 Pro IMH YC Dual -> ALC287_FIXUP_TAS2781_I2C */
  { 0x17aa, 0x38d4, 0xffff, 0x0072 },  /* Yoga S990-16 Pro IMH VECO Dual -> ALC287_FIXUP_TAS2781_I2C */
  { 0x17aa, 0x38d5, 0xffff, 0x0072 },  /* Yoga S990-16 Pro IMH YC Quad -> ALC287_FIXUP_TAS2781_I2C */
  { 0x17aa, 0x38d6, 0xffff, 0x0072 },  /* Yoga S990-16 Pro IMH VECO Quad -> ALC287_FIXUP_TAS2781_I2C */
  { 0x17aa, 0x38d7, 0xffff, 0x00da },  /* Lenovo Yoga 9 14IMH9 -> ALC287_FIXUP_YOGA9_14IMH9_BASS_SPK_PIN */
  { 0x17aa, 0x38dc, 0xffff, 0x7fff },  /* Lenovo ThinkBook G8 -> ALC285_FIXUP_SPEAKER2_TO_DAC1 (nothing to do) */
  { 0x17aa, 0x38df, 0xffff, 0x0072 },  /* Yoga Y990 Intel YC Dual -> ALC287_FIXUP_TAS2781_I2C */
  { 0x17aa, 0x38e0, 0xffff, 0x0072 },  /* Yoga Y990 Intel VECO Dual -> ALC287_FIXUP_TAS2781_I2C */
  { 0x17aa, 0x38f8, 0xffff, 0x0072 },  /* Yoga Book 9i -> ALC287_FIXUP_TAS2781_I2C */
  { 0x17aa, 0x395b, 0xffff, 0x80dd },  /* Lenovo Legion 7 15ASH11 [codec SSID] -> ALC287_FIXUP_LENOVO_YOGA_PRO7 */
  { 0x17aa, 0x38f9, 0xffff, 0x00ce },  /* Thinkbook 16P Gen5 -> ALC287_FIXUP_MG_RTKC_CSAMP_CS35L41_I2C_THINKPAD */
  { 0x17aa, 0x38fa, 0xffff, 0x00ce },  /* Thinkbook 16P Gen5 -> ALC287_FIXUP_MG_RTKC_CSAMP_CS35L41_I2C_THINKPAD */
  { 0x17aa, 0x38fc, 0xffff, 0x00dd },  /* Lenovo Yoga Pro 7 15ASH11 -> ALC287_FIXUP_LENOVO_YOGA_PRO7 */
  { 0x17aa, 0x38fd, 0xffff, 0x0072 },  /* ThinkBook plus Gen5 Hybrid -> ALC287_FIXUP_TAS2781_I2C */
  { 0x17aa, 0x3902, 0xffff, 0x00df },  /* Lenovo E50-80 -> ALC269_FIXUP_DMIC_THINKPAD_ACPI */
  { 0x17aa, 0x3906, 0xffff, 0xffff },  /* Legion Pro 7i 16IAX10H / Y9000P IAX10 [codec SSID] -> ALC287_FIXUP_LENOVO_LEGION_AW88399 (nothing to do) */
  { 0x17aa, 0x3907, 0xffff, 0xffff },  /* Legion Pro 7i 16IAX10H / Y9000P IAX10 [codec SSID] -> ALC287_FIXUP_LENOVO_LEGION_AW88399 (nothing to do) */
  { 0x17aa, 0x390d, 0xffff, 0x00e0 },  /* Lenovo Yoga Pro 7 14ASP10 -> ALC287_FIXUP_YOGA9_14IAP7_BASS_SPK_PIN_HEADSET */
  { 0x17aa, 0x3911, 0xffff, 0x00d1 },  /* Lenovo Yoga Pro 7 14IAH10 -> ALC287_FIXUP_YOGA9_14IAP7_BASS_SPK_PIN */
  { 0x17aa, 0x3912, 0xffff, 0x00d1 },  /* Lenovo Xiaoxin 14 GT -> ALC287_FIXUP_YOGA9_14IAP7_BASS_SPK_PIN */
  { 0x17aa, 0x3913, 0xffff, 0x00e1 },  /* Lenovo 145 -> ALC236_FIXUP_LENOVO_INV_DMIC */
  { 0x17aa, 0x391f, 0xffff, 0x00aa },  /* Yoga S990-16 pro Quad YC Quad -> ALC287_FIXUP_TXNW2781_I2C */
  { 0x17aa, 0x3920, 0xffff, 0x00e3 },  /* Yoga S990-16 pro Quad VECO Quad -> ALC287_FIXUP_YOGA9_SPEAKER2_TO_DAC1 */
  { 0x17aa, 0x3929, 0xffff, 0x00ce },  /* Thinkbook 13x Gen 5 -> ALC287_FIXUP_MG_RTKC_CSAMP_CS35L41_I2C_THINKPAD */
  { 0x17aa, 0x392b, 0xffff, 0x00ce },  /* Thinkbook 13x Gen 5 -> ALC287_FIXUP_MG_RTKC_CSAMP_CS35L41_I2C_THINKPAD */
  { 0x17aa, 0x3936, 0xffff, 0xffff },  /* Legion R9000P ADR10H [codec SSID] -> ALC287_FIXUP_LENOVO_LEGION_AW88399 (nothing to do) */
  { 0x17aa, 0x3937, 0xffff, 0xffff },  /* Legion R9000P ADR10H [codec SSID] -> ALC287_FIXUP_LENOVO_LEGION_AW88399 (nothing to do) */
  { 0x17aa, 0x3938, 0xffff, 0xffff },  /* Legion Pro 7 16AFR10H [codec SSID] -> ALC287_FIXUP_LENOVO_LEGION_AW88399 (nothing to do) */
  { 0x17aa, 0x3939, 0xffff, 0xffff },  /* Legion Pro 7 16AFR10H [codec SSID] -> ALC287_FIXUP_LENOVO_LEGION_AW88399 (nothing to do) */
  { 0x17aa, 0x393e, 0xffff, 0x00e4 },  /* Lenovo ThinkBook 14 G8+ IPH -> ALC287_FIXUP_LENOVO_XPAD_HEADSET_JACK */
  { 0x17aa, 0x394c, 0xffff, 0x80d1 },  /* Lenovo Yoga Slim 7 14AGP11 [codec SSID] -> ALC287_FIXUP_YOGA9_14IAP7_BASS_SPK_PIN */
  { 0x17aa, 0x3977, 0xffff, 0x00e2 },  /* IdeaPad S210 -> ALC283_FIXUP_INT_MIC */
  { 0x17aa, 0x3978, 0xffff, 0x00df },  /* Lenovo B50-70 -> ALC269_FIXUP_DMIC_THINKPAD_ACPI */
  { 0x17aa, 0x3bf8, 0xffff, 0x7fff },  /* Quanta FL1 -> ALC269_FIXUP_PCM_44K (nothing to do) */
  { 0x17aa, 0x5013, 0xffff, 0x00c8 },  /* Thinkpad -> ALC269_FIXUP_THINKPAD_LIMIT_INT_MIC_BOOST */
  { 0x17aa, 0x501a, 0xffff, 0x00e2 },  /* Thinkpad -> ALC283_FIXUP_INT_MIC */
  { 0x17aa, 0x501e, 0xffff, 0x7fff },  /* Thinkpad L440 -> ALC292_FIXUP_TPT440_DOCK (nothing to do) */
  { 0x17aa, 0x5026, 0xffff, 0x00c8 },  /* Thinkpad -> ALC269_FIXUP_THINKPAD_LIMIT_INT_MIC_BOOST */
  { 0x17aa, 0x5034, 0xffff, 0x7fff },  /* Thinkpad T450 -> ALC292_FIXUP_TPT440_DOCK (nothing to do) */
  { 0x17aa, 0x5036, 0xffff, 0x7fff },  /* Thinkpad T450s -> ALC292_FIXUP_TPT440_DOCK (nothing to do) */
  { 0x17aa, 0x503c, 0xffff, 0x7fff },  /* Thinkpad L450 -> ALC292_FIXUP_TPT440_DOCK (nothing to do) */
  { 0x17aa, 0x504a, 0xffff, 0x7fff },  /* ThinkPad X260 -> ALC292_FIXUP_TPT440_DOCK (nothing to do) */
  { 0x17aa, 0x504b, 0xffff, 0x00cb },  /* Thinkpad -> ALC293_FIXUP_LENOVO_SPK_NOISE */
  { 0x17aa, 0x5050, 0xffff, 0x00cc },  /* Thinkpad T560p -> ALC292_FIXUP_TPT460 */
  { 0x17aa, 0x5051, 0xffff, 0x00cc },  /* Thinkpad L460 -> ALC292_FIXUP_TPT460 */
  { 0x17aa, 0x5053, 0xffff, 0x00cc },  /* Thinkpad T460 -> ALC292_FIXUP_TPT460 */
  { 0x17aa, 0x505d, 0xffff, 0x00c9 },  /* Thinkpad -> ALC298_FIXUP_TPT470_DOCK */
  { 0x17aa, 0x505f, 0xffff, 0x00c9 },  /* Thinkpad -> ALC298_FIXUP_TPT470_DOCK */
  { 0x17aa, 0x5062, 0xffff, 0x00c9 },  /* Thinkpad -> ALC298_FIXUP_TPT470_DOCK */
  { 0x17aa, 0x508b, 0xffff, 0x00d4 },  /* Thinkpad X12 Gen 1 -> ALC287_FIXUP_LEGION_15IMHG05_SPEAKERS */
  { 0x17aa, 0x5109, 0xffff, 0x00c8 },  /* Thinkpad -> ALC269_FIXUP_THINKPAD_LIMIT_INT_MIC_BOOST */
  { 0x17aa, 0x511e, 0xffff, 0x00c9 },  /* Thinkpad -> ALC298_FIXUP_TPT470_DOCK */
  { 0x17aa, 0x511f, 0xffff, 0x00c9 },  /* Thinkpad -> ALC298_FIXUP_TPT470_DOCK */
  { 0x17aa, 0x9e54, 0xffff, 0x00e5 },  /* LENOVO NB -> ALC269_FIXUP_LENOVO_EAPD */
  { 0x17aa, 0x9e56, 0xffff, 0x00b2 },  /* Lenovo ZhaoYang CF4620Z -> ALC286_FIXUP_SONY_MIC_NO_PRESENCE */
  { 0x1849, 0x0269, 0xffff, 0x0080 },  /* Positivo Master C6400 -> ALC269VB_FIXUP_ASUS_ZENBOOK */
  { 0x1849, 0x1233, 0xffff, 0x7fff },  /* ASRock NUC Box 1100 -> ALC233_FIXUP_NO_AUDIO_JACK (nothing to do) */
  { 0x1849, 0x6256, 0xffff, 0x7fff },  /* ASRock NUC Box 358H -> ALC233_FIXUP_NO_AUDIO_JACK (nothing to do) */
  { 0x1849, 0xa233, 0xffff, 0x7fff },  /* Positivo Master C6300 -> ALC269_FIXUP_HEADSET_MIC (nothing to do) */
  { 0x1854, 0x0440, 0xffff, 0x7fff },  /* LG CQ6 -> ALC256_FIXUP_HEADPHONE_AMP_VOL (nothing to do) */
  { 0x1854, 0x0441, 0xffff, 0x7fff },  /* LG CQ6 AIO -> ALC256_FIXUP_HEADPHONE_AMP_VOL (nothing to do) */
  { 0x1854, 0x0488, 0xffff, 0x7fff },  /* LG gram 16 (16Z90R) -> ALC298_FIXUP_SAMSUNG_AMP_V2_4_AMPS (nothing to do) */
  { 0x1854, 0x0489, 0xffff, 0x7fff },  /* LG gram 16 (16Z90R-A) -> ALC298_FIXUP_SAMSUNG_AMP_V2_4_AMPS (nothing to do) */
  { 0x1854, 0x048a, 0xffff, 0x7fff },  /* LG gram 17 (17ZD90R) -> ALC298_FIXUP_SAMSUNG_AMP_V2_4_AMPS (nothing to do) */
  { 0x1854, 0x0490, 0xffff, 0x7fff },  /* LG Gram Style 14 (14Z90RS) -> ALC298_FIXUP_LG_GRAM_STYLE_14 (nothing to do) */
  { 0x1854, 0x0554, 0xffff, 0x7fff },  /* LG gram 16 (16Z90TR) -> ALC298_FIXUP_SAMSUNG_AMP_V2_2_AMPS (nothing to do) */
  { 0x19e5, 0x3204, 0xffff, 0x00e6 },  /* Huawei MACH-WX9 -> ALC256_FIXUP_HUAWEI_MACH_WX9_PINS */
  { 0x19e5, 0x320f, 0xffff, 0x006c },  /* Huawei WRT-WX9  -> ALC256_FIXUP_ASUS_MIC_NO_PRESENCE */
  { 0x19e5, 0x3212, 0xffff, 0x0015 },  /* Huawei KLV-WX9  -> ALC256_FIXUP_ACER_HEADSET_MIC */
  { 0x1a58, 0x2023, 0xffff, 0x00e7 },  /* Razer Blade 16 (2025) -> ALC298_FIXUP_RAZER_BLADE16_2025_PINS */
  { 0x1a58, 0x300e, 0xffff, 0x00e7 },  /* Razer Blade 16 (2025) -> ALC298_FIXUP_RAZER_BLADE16_2025_PINS */
  { 0x1b35, 0x1235, 0xffff, 0x00e9 },  /* CZC B20 -> ALC269_FIXUP_CZC_B20 */
  { 0x1b35, 0x1236, 0xffff, 0x00ea },  /* CZC TMI -> ALC269_FIXUP_CZC_TMI */
  { 0x1b35, 0x1237, 0xffff, 0x00eb },  /* CZC L101 -> ALC269_FIXUP_CZC_L101 */
  { 0x1b7d, 0xa831, 0xffff, 0x00ec },  /* Ordissimo EVE2  -> ALC269VB_FIXUP_ORDISSIMO_EVE2 */
  { 0x1c06, 0x2013, 0xffff, 0x00ed },  /* Lemote A1802 -> ALC269_FIXUP_LEMOTE_A1802 */
  { 0x1c06, 0x2015, 0xffff, 0x00ee },  /* Lemote A190X -> ALC269_FIXUP_LEMOTE_A190X */
  { 0x1c6c, 0x122a, 0xffff, 0x7fff },  /* Positivo N14AP7 -> ALC269_FIXUP_LIMIT_INT_MIC_BOOST (nothing to do) */
  { 0x1c6c, 0x1251, 0xffff, 0x0031 },  /* Positivo N14KP6-TG -> ALC288_FIXUP_DELL1_MIC_NO_PRESENCE */
  { 0x1d05, 0x1132, 0xffff, 0x7fff },  /* TongFang PHxTxX1 -> ALC256_FIXUP_SET_COEF_DEFAULTS (nothing to do) */
  { 0x1d05, 0x1096, 0xffff, 0x7fff },  /* TongFang GMxMRxx -> ALC269_FIXUP_NO_SHUTUP (nothing to do) */
  { 0x1d05, 0x1100, 0xffff, 0x7fff },  /* TongFang GKxNRxx -> ALC269_FIXUP_NO_SHUTUP (nothing to do) */
  { 0x1d05, 0x1111, 0xffff, 0x7fff },  /* TongFang GMxZGxx -> ALC269_FIXUP_NO_SHUTUP (nothing to do) */
  { 0x1d05, 0x1119, 0xffff, 0x7fff },  /* TongFang GMxZGxx -> ALC269_FIXUP_NO_SHUTUP (nothing to do) */
  { 0x1d05, 0x1129, 0xffff, 0x7fff },  /* TongFang GMxZGxx -> ALC269_FIXUP_NO_SHUTUP (nothing to do) */
  { 0x1d05, 0x1147, 0xffff, 0x7fff },  /* TongFang GMxTGxx -> ALC269_FIXUP_NO_SHUTUP (nothing to do) */
  { 0x1d05, 0x115c, 0xffff, 0x7fff },  /* TongFang GMxTGxx -> ALC269_FIXUP_NO_SHUTUP (nothing to do) */
  { 0x1d05, 0x121b, 0xffff, 0x7fff },  /* TongFang GMxAGxx -> ALC269_FIXUP_NO_SHUTUP (nothing to do) */
  { 0x1d05, 0x1387, 0xffff, 0x7fff },  /* TongFang GMxIXxx -> ALC2XX_FIXUP_HEADSET_MIC (nothing to do) */
  { 0x1d05, 0x1409, 0xffff, 0x7fff },  /* TongFang GMxIXxx -> ALC2XX_FIXUP_HEADSET_MIC (nothing to do) */
  { 0x1d05, 0x300f, 0xffff, 0x7fff },  /* TongFang X6AR5xxY -> ALC2XX_FIXUP_HEADSET_MIC (nothing to do) */
  { 0x1d05, 0x3019, 0xffff, 0x7fff },  /* TongFang X6FR5xxY -> ALC2XX_FIXUP_HEADSET_MIC (nothing to do) */
  { 0x1d05, 0x3031, 0xffff, 0x7fff },  /* TongFang X6AR55xU -> ALC2XX_FIXUP_HEADSET_MIC (nothing to do) */
  { 0x1d05, 0x3033, 0xffff, 0x7fff },  /* TongFang X6SP45xU -> ALC2XX_FIXUP_HEADSET_MIC (nothing to do) */
  { 0x1d05, 0x3034, 0xffff, 0x7fff },  /* TongFang X6KK45xU -> ALC2XX_FIXUP_HEADSET_MIC (nothing to do) */
  { 0x1d05, 0x30ba, 0xffff, 0x7fff },  /* TongFang XxAF5xxx -> ALC2XX_FIXUP_HEADSET_MIC (nothing to do) */
  { 0x1d17, 0x3288, 0xffff, 0x0013 },  /* Haier Boyue G42 -> ALC269VC_FIXUP_ACER_VCOPPERBOX_PINS */
  { 0x1d19, 0x0006, 0xffff, 0x001b },  /* VAIO VJS131 -> ALC233_FIXUP_ASUS_MIC_NO_PRESENCE */
  { 0x1d72, 0x1602, 0xffff, 0x001f },  /* RedmiBook -> ALC255_FIXUP_XIAOMI_HEADSET_MIC */
  { 0x1d72, 0x1701, 0xffff, 0x00ef },  /* XiaomiNotebook Pro -> ALC298_FIXUP_DELL1_MIC_NO_PRESENCE */
  { 0x1d72, 0x1901, 0xffff, 0x0095 },  /* RedmiBook 14 -> ALC256_FIXUP_ASUS_HEADSET_MIC */
  { 0x1d72, 0x1905, 0xffff, 0x7fff },  /* Xiaomi Mi Laptop Pro 15 -> ALC256_FIXUP_XIAOMI_PRO15_RESUME (nothing to do) */
  { 0x1d72, 0x1945, 0xffff, 0x0095 },  /* Redmi G -> ALC256_FIXUP_ASUS_HEADSET_MIC */
  { 0x1d72, 0x1947, 0xffff, 0x001f },  /* RedmiBook Air -> ALC255_FIXUP_XIAOMI_HEADSET_MIC */
  { 0x1e39, 0xca14, 0xffff, 0x00f0 },  /* MEDION NM14LNL -> ALC233_FIXUP_MEDION_MTL_SPK */
  { 0x1e50, 0x7007, 0xffff, 0x7fff },  /* Positivo DN50E -> ALC269_FIXUP_LIMIT_INT_MIC_BOOST (nothing to do) */
  { 0x1e50, 0x7036, 0xffff, 0x00f1 },  /* Acer Gadget E10 ETBook -> ALC233_FIXUP_WUJIE_SPEAKERS */
  { 0x1e50, 0x7038, 0xffff, 0x7fff },  /* Positivo DN140 -> ALC269_FIXUP_LIMIT_INT_MIC_BOOST (nothing to do) */
  { 0x1ee7, 0x2078, 0xffff, 0x7fff },  /* HONOR BRB-X M1010 -> ALC2XX_FIXUP_HEADSET_MIC (nothing to do) */
  { 0x1ee7, 0x2081, 0xffff, 0x00f2 },  /* HONOR MRB-XXX M1020 -> ALC256_FIXUP_HONOR_MRB_XXX_M1020_AUDIO */
  { 0x1f4c, 0xb020, 0xffff, 0x7fff },  /* Minisforum AI X1 Pro -> ALC245_FIXUP_MINISFORUM_JACK_DETECT (nothing to do) */
  { 0x1f4c, 0xe001, 0xffff, 0x7fff },  /* Minisforum V3 (SE) -> ALC245_FIXUP_BASS_HP_DAC (nothing to do) */
  { 0x1f66, 0x0105, 0xffff, 0x7fff },  /* Ayaneo Portable Game Player -> ALC287_FIXUP_CS35L41_I2C_2 (nothing to do) */
  { 0x2014, 0x800a, 0xffff, 0x7fff },  /* Positivo ARN50 -> ALC269_FIXUP_LIMIT_INT_MIC_BOOST (nothing to do) */
  { 0x2039, 0x0001, 0xffff, 0x7fff },  /* Inspur S14-G1 -> ALC295_FIXUP_CHROME_BOOK (nothing to do) */
  { 0x2145, 0x0001, 0xffff, 0x7fff },  /* Star Labs StarFighter -> ALC233_FIXUP_STARLABS_STARFIGHTER (nothing to do) */
  { 0x2782, 0x0206, 0xffff, 0x00f3 },  /* IPASON SmartBook S1 -> ALC256_FIXUP_IPASON_SMARTBOOK_S1 */
  { 0x2782, 0x0214, 0xffff, 0x7fff },  /* VAIO VJFE-CL -> ALC269_FIXUP_LIMIT_INT_MIC_BOOST (nothing to do) */
  { 0x2782, 0x0228, 0xffff, 0x00f4 },  /* Infinix ZERO BOOK 13 -> ALC269VB_FIXUP_INFINIX_ZERO_BOOK_13 */
  { 0x2782, 0x0232, 0xffff, 0x00f5 },  /* CHUWI CoreBook XPro -> ALC269VB_FIXUP_CHUWI_COREBOOK_XPRO */
  { 0x2782, 0x0250, 0xffff, 0x00f6 },  /* Infinix INBOOK X3 Slim -> ALC269VB_FIXUP_INFINIX_INBOOK_X3_SLIM */
  { 0x2782, 0x1407, 0xffff, 0x00f7 },  /* Positivo P15X -> ALC269_FIXUP_POSITIVO_P15X_HEADSET_MIC */
  { 0x2782, 0x1409, 0xffff, 0x00f7 },  /* Positivo K116J -> ALC269_FIXUP_POSITIVO_P15X_HEADSET_MIC */
  { 0x2782, 0x1701, 0xffff, 0x00f8 },  /* Infinix Y4 Max -> ALC269VC_FIXUP_INFINIX_Y4_MAX */
  { 0x2782, 0x1705, 0xffff, 0x00f8 },  /* MEDION E15433 -> ALC269VC_FIXUP_INFINIX_Y4_MAX */
  { 0x2782, 0x1707, 0xffff, 0x0037 },  /* Vaio VJFE-ADL -> ALC298_FIXUP_SPK_VOLUME */
  { 0x2782, 0x4900, 0xffff, 0x00f0 },  /* MEDION E15443 -> ALC233_FIXUP_MEDION_MTL_SPK */
  { 0x2782, 0xa128, 0xffff, 0x7fff },  /* Positivo N15RPE-S -> ALC269_FIXUP_LIMIT_INT_MIC_BOOST (nothing to do) */
  { 0x2782, 0xa212, 0xffff, 0x00f9 },  /* Lunnen Ground 14 -> ALC269VC_FIXUP_LUNNEN_GROUND_14 */
  { 0x7017, 0x2014, 0xffff, 0x7fff },  /* Star Labs StarFighter -> ALC233_FIXUP_STARLABS_STARFIGHTER (nothing to do) */
  { 0x8086, 0x2073, 0xffff, 0x7fff },  /* Intel NUC 8 Hades Canyon -> ALC700_FIXUP_INTEL_HADES_CANYON (nothing to do) */
  { 0x8086, 0x2074, 0xffff, 0x7fff },  /* Intel NUC 8 -> ALC233_FIXUP_INTEL_NUC8_DMIC (nothing to do) */
  { 0x8086, 0x2080, 0xffff, 0x00fa },  /* Intel NUC 8 Rugged -> ALC256_FIXUP_INTEL_NUC8_RUGGED */
  { 0x8086, 0x2081, 0xffff, 0x00fb },  /* Intel NUC 10 -> ALC256_FIXUP_INTEL_NUC10 */
  { 0x8086, 0x3038, 0xffff, 0x7fff },  /* Intel NUC 13 -> ALC295_FIXUP_CHROME_BOOK (nothing to do) */
  { 0xc011, 0x1d05, 0xffff, 0x00f1 },  /* MECHREVO WUJIE Series -> ALC233_FIXUP_WUJIE_SPEAKERS */
  { 0xf111, 0x0001, 0xffff, 0x00fc },  /* Framework Laptop -> ALC295_FIXUP_FRAMEWORK_LAPTOP_MIC_NO_PRESENCE */
  { 0xf111, 0x0006, 0xffff, 0x00fc },  /* Framework Laptop -> ALC295_FIXUP_FRAMEWORK_LAPTOP_MIC_NO_PRESENCE */
  { 0xf111, 0x0009, 0xffff, 0x00fc },  /* Framework Laptop -> ALC295_FIXUP_FRAMEWORK_LAPTOP_MIC_NO_PRESENCE */
  { 0xf111, 0x000b, 0xffff, 0x00fc },  /* Framework Laptop -> ALC295_FIXUP_FRAMEWORK_LAPTOP_MIC_NO_PRESENCE */
  { 0xf111, 0x000c, 0xffff, 0x00fc },  /* Framework Laptop -> ALC295_FIXUP_FRAMEWORK_LAPTOP_MIC_NO_PRESENCE */
  { 0xf111, 0x000f, 0xffff, 0x00fd },  /* Framework Laptop 13 Pro PTL -> ALC295_FIXUP_FRAMEWORK_LAPTOP_LIMIT_INT_MIC_BOOST */
  { 0xf111, 0x010f, 0xffff, 0x00fd },  /* Framework Laptop 13 PTL -> ALC295_FIXUP_FRAMEWORK_LAPTOP_LIMIT_INT_MIC_BOOST */
  { 0xf111, 0x0010, 0xffff, 0x00fd },  /* Framework Laptop -> ALC295_FIXUP_FRAMEWORK_LAPTOP_LIMIT_INT_MIC_BOOST */
};

/* alc882: sound/hda/codecs/realtek/alc882.c  (alc882_fixups + alc882_fixup_tbl) */
static CONST HAK_HDA_CODEC_ID mHakIds_alc882[] = {
  { 0x10ec0662, 0x100002 },  /* ALC662 rev2 */
  { 0x10ec0882, 0x000000 },  /* ALC882 */
  { 0x10ec0883, 0x000000 },  /* ALC883 */
  { 0x10ec0885, 0x100101 },  /* ALC889A */
  { 0x10ec0885, 0x100103 },  /* ALC889A */
  { 0x10ec0885, 0x000000 },  /* ALC885 */
  { 0x10ec0887, 0x000000 },  /* ALC887 */
  { 0x10ec0888, 0x100101 },  /* ALC1200 */
  { 0x10ec0888, 0x000000 },  /* ALC888 */
  { 0x10ec0889, 0x000000 },  /* ALC889 */
  { 0x10ec0899, 0x000000 },  /* ALC898 */
  { 0x10ec0900, 0x000000 },  /* ALC1150 */
  { 0x10ec0b00, 0x000000 },  /* ALCS1200A */
  { 0x10ec1168, 0x000000 },  /* ALC1220 */
  { 0x10ec1220, 0x000000 },  /* ALC1220 */
};
static CONST HAK_HDA_PINCFG mHakPins_alc882[] = {
  { 0x16, 0x99130111 },
  { 0x17, 0x99130112 },
  { 0x00, 0x00000000 },
  { 0x16, 0x99130111 },
  { 0x1b, 0x99130112 },
  { 0x00, 0x00000000 },
  { 0x19, 0x00000021 },
  { 0x00, 0x00000000 },
  { 0x16, 0x99130110 },
  { 0x00, 0x00000000 },
  { 0x15, 0x02a14150 },
  { 0x19, 0x22219420 },
  { 0x00, 0x00000000 },
  { 0x16, 0x99130130 },
  { 0x00, 0x00000000 },
  { 0x18, 0x00000021 },
  { 0x19, 0x00000021 },
  { 0x00, 0x00000000 },
  { 0x17, 0x90170111 },
  { 0x00, 0x00000000 },
  { 0x18, 0x00000025 },
  { 0x00, 0x00000000 },
  { 0x1b, 0x02214120 },
  { 0x00, 0x00000000 },
  { 0x1c, 0x993301f0 },
  { 0x00, 0x00000000 },
  { 0x15, 0x01080104 },
  { 0x16, 0x01011012 },
  { 0x17, 0x01016011 },
  { 0x00, 0x00000000 },
  { 0x19, 0x01a1913c },
  { 0x00, 0x00000000 },
  { 0x15, 0x99130112 },
  { 0x16, 0x99130111 },
  { 0x00, 0x00000000 },
};
static CONST UINT32 mHakVerbs_alc882[] = {
  0x20050007,
  0x20043050,
  0x00000000,
  0x20050003,
  0x20040000,
  0x20050008,
  0x20040000,
  0x2005000b,
  0x20040003,
  0x20050007,
  0x20043050,
  0x00000000,
  0x20050007,
  0x20043060,
  0x00000000,
  0x2005000b,
  0x20040838,
  0x00000000,
  0x20050007,
  0x20040068,
  0x00000000,
  0x20050007,
  0x20043070,
  0x00000000,
};
static CONST HAK_HDA_FIXUP mHakFixups_alc882[] = {
  { 2, 0, 0, 0, 0x0 },  /* 0 ALC883_FIXUP_ACER_EAPD */
  { 1, 1, 2, 0, 0x0 },  /* 1 ALC882_FIXUP_ACER_ASPIRE_4930G */
  { 3, 0, 0, HAK_HDA_FN_GPIO_HIGH, 0x1 },  /* 2 ALC882_FIXUP_GPIO1 alc_fixup_gpio1() */
  { 1, 1, 4, 3, 0x0 },  /* 3 ALC882_FIXUP_ACER_ASPIRE_8930G */
  { 2, 1, 2, 3, 0x0 },  /* 4 ALC882_FIXUP_ASPIRE_8930G_VERBS */
  { 4, 0, 0, 6, 0x0 },  /* 5 ALC882_FIXUP_PB_M5210 */
  { 3, 0, 0, HAK_HDA_FN_SKU_IGNORE, 0x0 },  /* 6 ALC882_FIXUP_ACER_ASPIRE_7736 alc_fixup_sku_ignore() */
  { 2, 0, 0, 12, 0x0 },  /* 7 ALC882_FIXUP_EAPD */
  { 1, 0, 0, 8, 0x0 },  /* 8 ALC882_FIXUP_ASUS_W90V */
  { 3, 1, 7, HAK_HDA_FN_GPIO_HIGH, 0x1 },  /* 9 ALC882_FIXUP_ASUS_W2JC alc_fixup_gpio1() */
  { 0, 1, 11, 0, 0x0 },  /* 10 ALC887_FIXUP_ASUS_HMIC alc887_fixup_asus_jack() NOT PORTED */
  { 1, 0, 0, 10, 0x0 },  /* 11 ALC887_FIXUP_ASUS_AUDIO */
  { 2, 0, 0, 15, 0x0 },  /* 12 ALC888_FIXUP_EEE1601 */
  { 1, 1, 14, 13, 0x0 },  /* 13 ALC887_FIXUP_ASUS_BASS */
  { 0, 0, 0, 0, 0x0 },  /* 14 ALC887_FIXUP_BASS_CHMAP alc_fixup_bass_chmap() NOT PORTED */
  { 3, 0, 0, HAK_HDA_FN_GPIO_HIGH, 0x3 },  /* 15 ALC882_FIXUP_GPIO3 alc_fixup_gpio3() */
  { 4, 0, 0, 15, 0x0 },  /* 16 ALCS1200A_FIXUP_MIC_VREF */
  { 1, 0, 0, 18, 0x0 },  /* 17 ALC889_FIXUP_VAIO_TT */
  { 0, 1, 2, 0, 0x0 },  /* 18 ALC889_FIXUP_MBP_VREF alc889_fixup_mbp_vref() NOT PORTED */
  { 0, 1, 20, 0, 0x0 },  /* 19 ALC889_FIXUP_MP11_VREF alc889_fixup_mba11_vref() NOT PORTED */
  { 3, 0, 0, HAK_HDA_FN_GPIO_HIGH, 0x3 },  /* 20 ALC885_FIXUP_MACPRO_GPIO alc885_fixup_macpro_gpio() */
  { 0, 1, 18, 0, 0x0 },  /* 21 ALC889_FIXUP_MBA11_VREF alc889_fixup_mba11_vref() NOT PORTED */
  { 0, 1, 18, 0, 0x0 },  /* 22 ALC889_FIXUP_MBA21_VREF alc889_fixup_mba21_vref() NOT PORTED */
  { 0, 1, 2, 0, 0x0 },  /* 23 ALC889_FIXUP_IMAC91_VREF alc889_fixup_imac91_vref() NOT PORTED */
  { 0, 1, 20, 0, 0x0 },  /* 24 ALC889_FIXUP_MP41_VREF alc889_fixup_mbp_vref() NOT PORTED */
  { 4, 0, 0, 20, 0x0 },  /* 25 ALC888VD_FIXUP_MIC_100VREF */
  { 2, 0, 0, 18, 0x0 },  /* 26 ALC886_FIXUP_EAPD */
  { 1, 1, 28, 22, 0x0 },  /* 27 ALC889_FIXUP_FRONT_HP_NO_PRESENCE */
  { 1, 0, 0, 24, 0x0 },  /* 28 ALC889_FIXUP_CD */
  { 3, 0, 0, HAK_HDA_FN_ALC_COEF_SET_BITS, 0x703c3 },  /* 29 ALC1220_FIXUP_CLEVO_P950 alc1220_fixup_clevo_p950() */
  { 1, 0, 0, 26, 0x0 },  /* 30 ALC882_FIXUP_ABIT_AW9D_MAX */
  { 1, 1, 32, 30, 0x0 },  /* 31 ALC1220_FIXUP_CLEVO_PB51ED_PINS */
  { 3, 0, 0, HAK_HDA_FN_ALC_COEF_SET_BITS, 0x703c3 },  /* 32 ALC1220_FIXUP_CLEVO_PB51ED alc1220_fixup_clevo_pb51ed() */
  { 0, 1, 7, 0, 0x0 },  /* 33 ALC898_FIXUP_CLEVO_P775TM1 alc898_fixup_clevo_p775tm1() NOT PORTED */
  { 2, 0, 0, 21, 0x0 },  /* 34 ALC883_FIXUP_EAPD */
  { 1, 0, 0, 32, 0x0 },  /* 35 ALC882_FIXUP_LENOVO_Y530 */
};
static CONST HAK_HDA_QUIRK mHakQuirks_alc882[] = {
  { 0x1025, 0x006c, 0xffff, 0x0000 },  /* Acer Aspire 9810 -> ALC883_FIXUP_ACER_EAPD */
  { 0x1025, 0x0090, 0xffff, 0x0000 },  /* Acer Aspire -> ALC883_FIXUP_ACER_EAPD */
  { 0x1025, 0x0107, 0xffff, 0x0000 },  /* Acer Aspire -> ALC883_FIXUP_ACER_EAPD */
  { 0x1025, 0x010a, 0xffff, 0x0000 },  /* Acer Ferrari 5000 -> ALC883_FIXUP_ACER_EAPD */
  { 0x1025, 0x0110, 0xffff, 0x0000 },  /* Acer Aspire -> ALC883_FIXUP_ACER_EAPD */
  { 0x1025, 0x0112, 0xffff, 0x0000 },  /* Acer Aspire 9303 -> ALC883_FIXUP_ACER_EAPD */
  { 0x1025, 0x0121, 0xffff, 0x0000 },  /* Acer Aspire 5920G -> ALC883_FIXUP_ACER_EAPD */
  { 0x1025, 0x013e, 0xffff, 0x0001 },  /* Acer Aspire 4930G -> ALC882_FIXUP_ACER_ASPIRE_4930G */
  { 0x1025, 0x013f, 0xffff, 0x0001 },  /* Acer Aspire 5930G -> ALC882_FIXUP_ACER_ASPIRE_4930G */
  { 0x1025, 0x0145, 0xffff, 0x0003 },  /* Acer Aspire 8930G -> ALC882_FIXUP_ACER_ASPIRE_8930G */
  { 0x1025, 0x0146, 0xffff, 0x0003 },  /* Acer Aspire 6935G -> ALC882_FIXUP_ACER_ASPIRE_8930G */
  { 0x1025, 0x0142, 0xffff, 0x0001 },  /* Acer Aspire 7730G -> ALC882_FIXUP_ACER_ASPIRE_4930G */
  { 0x1025, 0x0155, 0xffff, 0x0005 },  /* Packard-Bell M5120 -> ALC882_FIXUP_PB_M5210 */
  { 0x1025, 0x015e, 0xffff, 0x0001 },  /* Acer Aspire 6930G -> ALC882_FIXUP_ACER_ASPIRE_4930G */
  { 0x1025, 0x0166, 0xffff, 0x0001 },  /* Acer Aspire 6530G -> ALC882_FIXUP_ACER_ASPIRE_4930G */
  { 0x1025, 0x021e, 0xffff, 0x0001 },  /* Acer Aspire 5739G -> ALC882_FIXUP_ACER_ASPIRE_4930G */
  { 0x1025, 0x0259, 0xffff, 0x7fff },  /* Acer Aspire 5935 -> ALC889_FIXUP_DAC_ROUTE (nothing to do) */
  { 0x1025, 0x026b, 0xffff, 0x0003 },  /* Acer Aspire 8940G -> ALC882_FIXUP_ACER_ASPIRE_8930G */
  { 0x1025, 0x0296, 0xffff, 0x0006 },  /* Acer Aspire 7736z -> ALC882_FIXUP_ACER_ASPIRE_7736 */
  { 0x1043, 0x13c2, 0xffff, 0x0007 },  /* Asus A7M -> ALC882_FIXUP_EAPD */
  { 0x1043, 0x1873, 0xffff, 0x0008 },  /* ASUS W90V -> ALC882_FIXUP_ASUS_W90V */
  { 0x1043, 0x1971, 0xffff, 0x0009 },  /* Asus W2JC -> ALC882_FIXUP_ASUS_W2JC */
  { 0x1043, 0x2390, 0xffff, 0x000a },  /* Asus D700SA -> ALC887_FIXUP_ASUS_HMIC */
  { 0x1043, 0x835f, 0xffff, 0x000c },  /* Asus Eee 1601 -> ALC888_FIXUP_EEE1601 */
  { 0x1043, 0x84bc, 0xffff, 0x000d },  /* ASUS ET2700 -> ALC887_FIXUP_ASUS_BASS */
  { 0x1043, 0x8691, 0xffff, 0x000f },  /* ASUS ROG Ranger VIII -> ALC882_FIXUP_GPIO3 */
  { 0x1043, 0x8797, 0xffff, 0x0010 },  /* ASUS TUF B550M-PLUS -> ALCS1200A_FIXUP_MIC_VREF */
  { 0x104d, 0x9043, 0xffff, 0x7fff },  /* Sony Vaio VGC-LN51JGB -> ALC882_FIXUP_NO_PRIMARY_HP (nothing to do) */
  { 0x104d, 0x9044, 0xffff, 0x7fff },  /* Sony VAIO AiO -> ALC882_FIXUP_NO_PRIMARY_HP (nothing to do) */
  { 0x104d, 0x9047, 0xffff, 0x0011 },  /* Sony Vaio TT -> ALC889_FIXUP_VAIO_TT */
  { 0x104d, 0x905a, 0xffff, 0x7fff },  /* Sony Vaio Z -> ALC882_FIXUP_NO_PRIMARY_HP (nothing to do) */
  { 0x104d, 0x9060, 0xffff, 0x7fff },  /* Sony Vaio VPCL14M1R -> ALC882_FIXUP_NO_PRIMARY_HP (nothing to do) */
  { 0x106b, 0x00a0, 0xffff, 0x0012 },  /* MacBookPro 3,1 -> ALC889_FIXUP_MBP_VREF */
  { 0x106b, 0x00a1, 0xffff, 0x0012 },  /* Macbook -> ALC889_FIXUP_MBP_VREF */
  { 0x106b, 0x00a4, 0xffff, 0x0012 },  /* MacbookPro 4,1 -> ALC889_FIXUP_MBP_VREF */
  { 0x106b, 0x0c00, 0xffff, 0x0013 },  /* Mac Pro -> ALC889_FIXUP_MP11_VREF */
  { 0x106b, 0x1000, 0xffff, 0x0014 },  /* iMac 24 -> ALC885_FIXUP_MACPRO_GPIO */
  { 0x106b, 0x2800, 0xffff, 0x0014 },  /* AppleTV -> ALC885_FIXUP_MACPRO_GPIO */
  { 0x106b, 0x2c00, 0xffff, 0x0012 },  /* MacbookPro rev3 -> ALC889_FIXUP_MBP_VREF */
  { 0x106b, 0x3000, 0xffff, 0x0012 },  /* iMac -> ALC889_FIXUP_MBP_VREF */
  { 0x106b, 0x3200, 0xffff, 0x0007 },  /* iMac 7,1 Aluminum -> ALC882_FIXUP_EAPD */
  { 0x106b, 0x3400, 0xffff, 0x0015 },  /* MacBookAir 1,1 -> ALC889_FIXUP_MBA11_VREF */
  { 0x106b, 0x3500, 0xffff, 0x0016 },  /* MacBookAir 2,1 -> ALC889_FIXUP_MBA21_VREF */
  { 0x106b, 0x3600, 0xffff, 0x0012 },  /* Macbook 3,1 -> ALC889_FIXUP_MBP_VREF */
  { 0x106b, 0x3800, 0xffff, 0x0012 },  /* MacbookPro 4,1 -> ALC889_FIXUP_MBP_VREF */
  { 0x106b, 0x3e00, 0xffff, 0x0014 },  /* iMac 24 Aluminum -> ALC885_FIXUP_MACPRO_GPIO */
  { 0x106b, 0x3f00, 0xffff, 0x0017 },  /* Macbook 5,1 -> ALC889_FIXUP_IMAC91_VREF */
  { 0x106b, 0x4000, 0xffff, 0x0017 },  /* MacbookPro 5,1 -> ALC889_FIXUP_IMAC91_VREF */
  { 0x106b, 0x4100, 0xffff, 0x0017 },  /* Macmini 3,1 -> ALC889_FIXUP_IMAC91_VREF */
  { 0x106b, 0x4200, 0xffff, 0x0018 },  /* Mac Pro 4,1/5,1 -> ALC889_FIXUP_MP41_VREF */
  { 0x106b, 0x4300, 0xffff, 0x0017 },  /* iMac 9,1 -> ALC889_FIXUP_IMAC91_VREF */
  { 0x106b, 0x4600, 0xffff, 0x0017 },  /* MacbookPro 5,2 -> ALC889_FIXUP_IMAC91_VREF */
  { 0x106b, 0x4900, 0xffff, 0x0017 },  /* iMac 9,1 Aluminum -> ALC889_FIXUP_IMAC91_VREF */
  { 0x106b, 0x4a00, 0xffff, 0x0015 },  /* Macbook 5,2 -> ALC889_FIXUP_MBA11_VREF */
  { 0x1071, 0x8258, 0xffff, 0x0007 },  /* Evesham Voyaeger -> ALC882_FIXUP_EAPD */
  { 0x10ec, 0x12d8, 0xffff, 0x0019 },  /* iBase Elo Touch -> ALC888VD_FIXUP_MIC_100VREF */
  { 0x13fe, 0x1009, 0xffff, 0x001a },  /* Advantech MIT-W101 -> ALC886_FIXUP_EAPD */
  { 0x1458, 0xa002, 0xffff, 0x001b },  /* Gigabyte EP45-DS3/Z87X-UD3H -> ALC889_FIXUP_FRONT_HP_NO_PRESENCE */
  { 0x1458, 0xa0b8, 0xffff, 0x7fff },  /* Gigabyte AZ370-Gaming -> ALC1220_FIXUP_GB_DUAL_CODECS (nothing to do) */
  { 0x1458, 0xa0cd, 0xffff, 0x7fff },  /* Gigabyte X570 Aorus Master -> ALC1220_FIXUP_GB_X570 (nothing to do) */
  { 0x1458, 0xa0ce, 0xffff, 0x7fff },  /* Gigabyte X570 Aorus Xtreme -> ALC1220_FIXUP_GB_X570 (nothing to do) */
  { 0x1458, 0xa0d5, 0xffff, 0x7fff },  /* Gigabyte X570S Aorus Master -> ALC1220_FIXUP_GB_X570 (nothing to do) */
  { 0x1462, 0x11f7, 0xffff, 0x001d },  /* MSI-GE63 -> ALC1220_FIXUP_CLEVO_P950 */
  { 0x1462, 0x1228, 0xffff, 0x001d },  /* MSI-GP63 -> ALC1220_FIXUP_CLEVO_P950 */
  { 0x1462, 0x1229, 0xffff, 0x001d },  /* MSI-GP73 -> ALC1220_FIXUP_CLEVO_P950 */
  { 0x1462, 0x1275, 0xffff, 0x001d },  /* MSI-GL63 -> ALC1220_FIXUP_CLEVO_P950 */
  { 0x1462, 0x1276, 0xffff, 0x001d },  /* MSI-GL73 -> ALC1220_FIXUP_CLEVO_P950 */
  { 0x1462, 0x1293, 0xffff, 0x001d },  /* MSI-GP65 -> ALC1220_FIXUP_CLEVO_P950 */
  { 0x1462, 0x7350, 0xffff, 0x001c },  /* MSI-7350 -> ALC889_FIXUP_CD */
  { 0x1462, 0xcc34, 0xffff, 0x7fff },  /* MSI Godlike X570 -> ALC1220_FIXUP_GB_DUAL_CODECS (nothing to do) */
  { 0x1462, 0xda57, 0xffff, 0x7fff },  /* MSI Z270-Gaming -> ALC1220_FIXUP_GB_DUAL_CODECS (nothing to do) */
  { 0x1462, 0x0000, 0x0000, 0x000f },  /* MSI -> ALC882_FIXUP_GPIO3 */
  { 0x147b, 0x107a, 0xffff, 0x001e },  /* Abit AW9D-MAX -> ALC882_FIXUP_ABIT_AW9D_MAX */
  { 0x1558, 0x3702, 0xffff, 0x001f },  /* Clevo X370SN[VW] -> ALC1220_FIXUP_CLEVO_PB51ED_PINS */
  { 0x1558, 0x50d3, 0xffff, 0x001f },  /* Clevo PC50[ER][CDF] -> ALC1220_FIXUP_CLEVO_PB51ED_PINS */
  { 0x1558, 0x5802, 0xffff, 0x001f },  /* Clevo X58[05]WN[RST] -> ALC1220_FIXUP_CLEVO_PB51ED_PINS */
  { 0x1558, 0x65d1, 0xffff, 0x001f },  /* Clevo PB51[ER][CDF] -> ALC1220_FIXUP_CLEVO_PB51ED_PINS */
  { 0x1558, 0x65d2, 0xffff, 0x001f },  /* Clevo PB51R[CDF] -> ALC1220_FIXUP_CLEVO_PB51ED_PINS */
  { 0x1558, 0x65e1, 0xffff, 0x001f },  /* Clevo PB51[ED][DF] -> ALC1220_FIXUP_CLEVO_PB51ED_PINS */
  { 0x1558, 0x65e5, 0xffff, 0x001f },  /* Clevo PC50D[PRS](?:-D|-G)? -> ALC1220_FIXUP_CLEVO_PB51ED_PINS */
  { 0x1558, 0x65f1, 0xffff, 0x001f },  /* Clevo PC50HS -> ALC1220_FIXUP_CLEVO_PB51ED_PINS */
  { 0x1558, 0x65f5, 0xffff, 0x001f },  /* Clevo PD50PN[NRT] -> ALC1220_FIXUP_CLEVO_PB51ED_PINS */
  { 0x1558, 0x66a2, 0xffff, 0x001f },  /* Clevo PE60RNE -> ALC1220_FIXUP_CLEVO_PB51ED_PINS */
  { 0x1558, 0x66a6, 0xffff, 0x001f },  /* Clevo PE60SN[CDE]-[GS] -> ALC1220_FIXUP_CLEVO_PB51ED_PINS */
  { 0x1558, 0x67d1, 0xffff, 0x001f },  /* Clevo PB71[ER][CDF] -> ALC1220_FIXUP_CLEVO_PB51ED_PINS */
  { 0x1558, 0x67e1, 0xffff, 0x001f },  /* Clevo PB71[DE][CDF] -> ALC1220_FIXUP_CLEVO_PB51ED_PINS */
  { 0x1558, 0x67e5, 0xffff, 0x001f },  /* Clevo PC70D[PRS](?:-D|-G)? -> ALC1220_FIXUP_CLEVO_PB51ED_PINS */
  { 0x1558, 0x67f1, 0xffff, 0x001f },  /* Clevo PC70H[PRS] -> ALC1220_FIXUP_CLEVO_PB51ED_PINS */
  { 0x1558, 0x67f5, 0xffff, 0x001f },  /* Clevo PD70PN[NRT] -> ALC1220_FIXUP_CLEVO_PB51ED_PINS */
  { 0x1558, 0x70d1, 0xffff, 0x001f },  /* Clevo PC70[ER][CDF] -> ALC1220_FIXUP_CLEVO_PB51ED_PINS */
  { 0x1558, 0x7709, 0xffff, 0x0021 },  /* Clevo P775TM1 -> ALC898_FIXUP_CLEVO_P775TM1 */
  { 0x1558, 0x7714, 0xffff, 0x001f },  /* Clevo X170SM -> ALC1220_FIXUP_CLEVO_PB51ED_PINS */
  { 0x1558, 0x7715, 0xffff, 0x0020 },  /* Clevo X170KM-G -> ALC1220_FIXUP_CLEVO_PB51ED */
  { 0x1558, 0x9501, 0xffff, 0x001d },  /* Clevo P950HR -> ALC1220_FIXUP_CLEVO_P950 */
  { 0x1558, 0x9506, 0xffff, 0x001d },  /* Clevo P955HQ -> ALC1220_FIXUP_CLEVO_P950 */
  { 0x1558, 0x950a, 0xffff, 0x001d },  /* Clevo P955H[PR] -> ALC1220_FIXUP_CLEVO_P950 */
  { 0x1558, 0x95e1, 0xffff, 0x001d },  /* Clevo P95xER -> ALC1220_FIXUP_CLEVO_P950 */
  { 0x1558, 0x95e2, 0xffff, 0x001d },  /* Clevo P950ER -> ALC1220_FIXUP_CLEVO_P950 */
  { 0x1558, 0x95e3, 0xffff, 0x001d },  /* Clevo P955[ER]T -> ALC1220_FIXUP_CLEVO_P950 */
  { 0x1558, 0x95e4, 0xffff, 0x001d },  /* Clevo P955ER -> ALC1220_FIXUP_CLEVO_P950 */
  { 0x1558, 0x95e5, 0xffff, 0x001d },  /* Clevo P955EE6 -> ALC1220_FIXUP_CLEVO_P950 */
  { 0x1558, 0x95e6, 0xffff, 0x001d },  /* Clevo P950R[CDF] -> ALC1220_FIXUP_CLEVO_P950 */
  { 0x1558, 0x96e1, 0xffff, 0x001d },  /* Clevo P960[ER][CDFN]-K -> ALC1220_FIXUP_CLEVO_P950 */
  { 0x1558, 0x97e1, 0xffff, 0x001d },  /* Clevo P970[ER][CDFN] -> ALC1220_FIXUP_CLEVO_P950 */
  { 0x1558, 0x97e2, 0xffff, 0x001d },  /* Clevo P970RC-M -> ALC1220_FIXUP_CLEVO_P950 */
  { 0x1558, 0xd502, 0xffff, 0x001f },  /* Clevo PD50SNE -> ALC1220_FIXUP_CLEVO_PB51ED_PINS */
  { 0x1558, 0x0000, 0x0000, 0x0007 },  /* Clevo laptop -> ALC882_FIXUP_EAPD */
  { 0x161f, 0x2054, 0xffff, 0x0022 },  /* Medion laptop -> ALC883_FIXUP_EAPD */
  { 0x17aa, 0x3a0d, 0xffff, 0x0023 },  /* Lenovo Y530 -> ALC882_FIXUP_LENOVO_Y530 */
  { 0x8086, 0x0022, 0xffff, 0x7fff },  /* DX58SO -> ALC889_FIXUP_COEF (nothing to do) */
};

/* alc662: sound/hda/codecs/realtek/alc662.c  (alc662_fixups + alc662_fixup_tbl) */
static CONST HAK_HDA_CODEC_ID mHakIds_alc662[] = {
  { 0x10ec0272, 0x000000 },  /* ALC272 */
  { 0x10ec0662, 0x100101 },  /* ALC662 rev1 */
  { 0x10ec0662, 0x100300 },  /* ALC662 rev3 */
  { 0x10ec0663, 0x000000 },  /* ALC663 */
  { 0x10ec0665, 0x000000 },  /* ALC665 */
  { 0x10ec0667, 0x000000 },  /* ALC667 */
  { 0x10ec0668, 0x000000 },  /* ALC668 */
  { 0x10ec0670, 0x000000 },  /* ALC670 */
  { 0x10ec0671, 0x000000 },  /* ALC671 */
  { 0x10ec0867, 0x000000 },  /* ALC891 */
  { 0x10ec0892, 0x000000 },  /* ALC892 */
  { 0x10ec0897, 0x000000 },  /* ALC897 */
};
static CONST HAK_HDA_PINCFG mHakPins_alc662[] = {
  { 0x14, 0x99130110 },
  { 0x18, 0x01a19820 },
  { 0x19, 0x99a3092f },
  { 0x1b, 0x0121401f },
  { 0x00, 0x00000000 },
  { 0x19, 0x03a11050 },
  { 0x00, 0x00000000 },
  { 0x15, 0x99130112 },
  { 0x00, 0x00000000 },
  { 0x15, 0x92130110 },
  { 0x18, 0x99130111 },
  { 0x1b, 0x11130012 },
  { 0x00, 0x00000000 },
  { 0x1a, 0x01a11140 },
  { 0x1b, 0x0221144f },
  { 0x00, 0x00000000 },
  { 0x1a, 0x02a1113c },
  { 0x00, 0x00000000 },
  { 0x19, 0x03a1913d },
  { 0x1b, 0x03a1113c },
  { 0x00, 0x00000000 },
  { 0x14, 0x0221201f },
  { 0x00, 0x00000000 },
  { 0x19, 0x01a1913c },
  { 0x00, 0x00000000 },
  { 0x1a, 0x80106111 },
  { 0x00, 0x00000000 },
  { 0x16, 0x0421101f },
  { 0x00, 0x00000000 },
  { 0x14, 0x99130110 },
  { 0x16, 0x99130111 },
  { 0x18, 0x01a19840 },
  { 0x19, 0x99a3094f },
  { 0x21, 0x0121441f },
  { 0x00, 0x00000000 },
  { 0x16, 0x80106111 },
  { 0x00, 0x00000000 },
  { 0x19, 0x03a1913d },
  { 0x1a, 0x90170151 },
  { 0x1b, 0x03a1113c },
  { 0x00, 0x00000000 },
  { 0x1b, 0x04a1112c },
  { 0x00, 0x00000000 },
  { 0x14, 0x99130110 },
  { 0x12, 0x99a30970 },
  { 0x15, 0x01214020 },
  { 0x17, 0x99130111 },
  { 0x18, 0x01a19840 },
  { 0x21, 0x0121401f },
  { 0x00, 0x00000000 },
  { 0x17, 0x99130112 },
  { 0x00, 0x00000000 },
  { 0x19, 0x02a1913c },
  { 0x18, 0x01a1903d },
  { 0x00, 0x00000000 },
  { 0x1a, 0x03a11050 },
  { 0x00, 0x00000000 },
  { 0x1a, 0x01a11140 },
  { 0x00, 0x00000000 },
  { 0x15, 0x40f000f0 },
  { 0x16, 0x40f000f0 },
  { 0x00, 0x00000000 },
  { 0x1b, 0x02214020 },
  { 0x00, 0x00000000 },
  { 0x15, 0x50170120 },
  { 0x00, 0x00000000 },
  { 0x12, 0x403cc000 },
  { 0x14, 0x90170110 },
  { 0x15, 0x411111f0 },
  { 0x16, 0x411111f0 },
  { 0x18, 0x01a19030 },
  { 0x19, 0x90a7013f },
  { 0x1a, 0x01014020 },
  { 0x1b, 0x0121401f },
  { 0x1c, 0x411111f0 },
  { 0x1d, 0x411111f0 },
  { 0x1e, 0x40478e35 },
  { 0x00, 0x00000000 },
};
static CONST UINT32 mHakVerbs_alc662[] = {
  0x200500c3,
  0x20044000,
  0x00000000,
  0x20050015,
  0x20040d60,
  0x00000000,
  0x14070c00,
  0x00000000,
};
static CONST HAK_HDA_FIXUP mHakFixups_alc662[] = {
  { 1, 1, 1, 0, 0x0 },  /* 0 ALC662_FIXUP_ASUS_MODE2 */
  { 3, 0, 0, HAK_HDA_FN_SKU_IGNORE, 0x0 },  /* 1 ALC662_FIXUP_SKU_IGNORE alc_fixup_sku_ignore() */
  { 1, 0, 0, 5, 0x0 },  /* 2 ALC897_FIXUP_HEADSET_MIC_PIN3 */
  { 1, 0, 0, 7, 0x0 },  /* 3 ALC662_FIXUP_ASPIRE */
  { 1, 1, 5, 9, 0x0 },  /* 4 ALC669_FIXUP_ACER_ASPIRE_ETHOS */
  { 0, 0, 0, 0, 0x0 },  /* 5 ALC669_FIXUP_ACER_ASPIRE_ETHOS_HEADSET alc662_fixup_aspire_ethos_hp() NOT PORTED */
  { 1, 1, 7, 13, 0x0 },  /* 6 ALC662_FIXUP_ACER_NITRO_HEADSET_MODE */
  { 0, 0, 0, 0, 0x0 },  /* 7 ALC662_FIXUP_USI_FUNC alc662_fixup_usi_headset_mic() NOT PORTED */
  { 1, 1, 7, 16, 0x0 },  /* 8 ALC662_FIXUP_ACER_X2660G_HEADSET_MODE */
  { 1, 1, 10, 18, 0x0 },  /* 9 ALC668_FIXUP_DELL_MIC_NO_PRESENCE */
  { 0, 0, 0, 0, 0x0 },  /* 10 ALC668_FIXUP_HEADSET_MODE alc_fixup_headset_mode_alc668() NOT PORTED */
  { 0, 1, 12, 0, 0x0 },  /* 11 ALC668_FIXUP_DELL_XPS13 alc_fixup_dell_xps13() NOT PORTED */
  { 0, 1, 9, 0, 0x0 },  /* 12 ALC668_FIXUP_DELL_DISABLE_AAMIX alc_fixup_disable_aamix() NOT PORTED */
  { 1, 1, 1, 21, 0x0 },  /* 13 ALC662_FIXUP_HP_RP5800 */
  { 1, 0, 0, 23, 0x0 },  /* 14 ALC897_FIXUP_HP_HSMIC_VERB */
  { 0, 1, 16, 0, 0x0 },  /* 15 ALC662_FIXUP_ASUS_Nx50 alc_fixup_auto_mute_via_amp() NOT PORTED */
  { 1, 1, 17, 25, 0x0 },  /* 16 ALC662_FIXUP_BASS_1A */
  { 0, 0, 0, 0, 0x0 },  /* 17 ALC662_FIXUP_BASS_CHMAP alc_fixup_bass_chmap() NOT PORTED */
  { 1, 1, 19, 27, 0x0 },  /* 18 ALC668_FIXUP_ASUS_G751 */
  { 2, 0, 0, 0, 0x0 },  /* 19 ALC668_FIXUP_MIC_COEF */
  { 0, 1, 21, 0, 0x0 },  /* 20 ALC662_FIXUP_BASS_MODE4_CHMAP alc_fixup_bass_chmap() NOT PORTED */
  { 1, 1, 1, 29, 0x0 },  /* 21 ALC662_FIXUP_ASUS_MODE4 */
  { 1, 1, 17, 35, 0x0 },  /* 22 ALC662_FIXUP_BASS_16 */
  { 1, 1, 24, 37, 0x0 },  /* 23 ALC668_FIXUP_ASUS_Nx51 */
  { 0, 0, 17, 0, 0x0 },  /* 24 ALC668_FIXUP_ASUS_Nx51_HEADSET_MODE alc_fixup_headset_mode_alc668() NOT PORTED */
  { 1, 1, 26, 41, 0x0 },  /* 25 ALC668_FIXUP_ASUS_NO_HEADSET_MIC */
  { 0, 1, 27, 0, 0x0 },  /* 26 ALC668_FIXUP_HEADSET_MIC alc_fixup_headset_mic() NOT PORTED */
  { 2, 0, 0, 3, 0x0 },  /* 27 ALC668_FIXUP_MIC_DET_COEF */
  { 1, 1, 1, 43, 0x0 },  /* 28 ALC662_FIXUP_ASUS_MODE8 */
  { 1, 1, 30, 50, 0x0 },  /* 29 ALC662_FIXUP_IDEAPAD */
  { 0, 0, 0, 0, 0x0 },  /* 30 ALC662_FIXUP_LED_GPIO1 alc662_fixup_led_gpio1() NOT PORTED */
  { 1, 1, 7, 52, 0x0 },  /* 31 ALC662_FIXUP_USI_HEADSET_MODE */
  { 1, 1, 33, 55, 0x0 },  /* 32 ALC897_FIXUP_HEADSET_MIC_PIN */
  { 0, 0, 0, 0, 0x0 },  /* 33 ALC897_FIXUP_LENOVO_HEADSET_MIC alc897_fixup_lenovo_headset_mic() NOT PORTED */
  { 1, 1, 35, 57, 0x0 },  /* 34 ALC897_FIXUP_HEADSET_MIC_PIN2 */
  { 0, 0, 0, 0, 0x0 },  /* 35 ALC897_FIXUP_LENOVO_HEADSET_MODE alc897_fixup_lenovo_headset_mode() NOT PORTED */
  { 1, 0, 0, 59, 0x0 },  /* 36 ALC892_FIXUP_ASROCK_MOBO */
  { 1, 0, 0, 62, 0x0 },  /* 37 ALC662_FIXUP_ZOTAC_Z68 */
  { 1, 0, 0, 64, 0x0 },  /* 38 ALC662_FIXUP_ACER_VERITON */
  { 1, 1, 1, 66, 0x0 },  /* 39 ALC662_FIXUP_CZC_ET26 */
  { 2, 0, 0, 6, 0x0 },  /* 40 ALC662_FIXUP_CZC_P10T */
  { 3, 0, 0, HAK_HDA_FN_GPIO_HIGH, 0x4 },  /* 41 ALC897_FIXUP_BEELINK_SER6_AMP alc_fixup_gpio4() */
};
static CONST HAK_HDA_QUIRK mHakQuirks_alc662[] = {
  { 0x1019, 0x9087, 0xffff, 0x0000 },  /* ECS -> ALC662_FIXUP_ASUS_MODE2 */
  { 0x1019, 0x9859, 0xffff, 0x0002 },  /* JP-IK LEAP W502 -> ALC897_FIXUP_HEADSET_MIC_PIN3 */
  { 0x1022, 0xc950, 0xffff, 0x7fff },  /* CSL Unity BF24B -> ALC662_FIXUP_CSL_GPIO (nothing to do) */
  { 0x1025, 0x022f, 0xffff, 0x7fff },  /* Acer Aspire One -> ALC662_FIXUP_INV_DMIC (nothing to do) */
  { 0x1025, 0x0241, 0xffff, 0x7fff },  /* Packard Bell DOTS -> ALC662_FIXUP_INV_DMIC (nothing to do) */
  { 0x1025, 0x0308, 0xffff, 0x0003 },  /* Acer Aspire 8942G -> ALC662_FIXUP_ASPIRE */
  { 0x1025, 0x031c, 0xffff, 0x0001 },  /* Gateway NV79 -> ALC662_FIXUP_SKU_IGNORE */
  { 0x1025, 0x0349, 0xffff, 0x7fff },  /* eMachines eM250 -> ALC662_FIXUP_INV_DMIC (nothing to do) */
  { 0x1025, 0x034a, 0xffff, 0x7fff },  /* Gateway LT27 -> ALC662_FIXUP_INV_DMIC (nothing to do) */
  { 0x1025, 0x038b, 0xffff, 0x0003 },  /* Acer Aspire 8943G -> ALC662_FIXUP_ASPIRE */
  { 0x1025, 0x0566, 0xffff, 0x0004 },  /* Acer Aspire Ethos 8951G -> ALC669_FIXUP_ACER_ASPIRE_ETHOS */
  { 0x1025, 0x123c, 0xffff, 0x0006 },  /* Acer Nitro N50-600 -> ALC662_FIXUP_ACER_NITRO_HEADSET_MODE */
  { 0x1025, 0x124e, 0xffff, 0x0008 },  /* Acer 2660G -> ALC662_FIXUP_ACER_X2660G_HEADSET_MODE */
  { 0x1028, 0x05d8, 0xffff, 0x0009 },  /* Dell -> ALC668_FIXUP_DELL_MIC_NO_PRESENCE */
  { 0x1028, 0x05db, 0xffff, 0x0009 },  /* Dell -> ALC668_FIXUP_DELL_MIC_NO_PRESENCE */
  { 0x1028, 0x05fe, 0xffff, 0x000b },  /* Dell XPS 15 -> ALC668_FIXUP_DELL_XPS13 */
  { 0x1028, 0x060a, 0xffff, 0x000b },  /* Dell XPS 13 -> ALC668_FIXUP_DELL_XPS13 */
  { 0x1028, 0x060d, 0xffff, 0x000b },  /* Dell M3800 -> ALC668_FIXUP_DELL_XPS13 */
  { 0x1028, 0x0625, 0xffff, 0x0009 },  /* Dell -> ALC668_FIXUP_DELL_MIC_NO_PRESENCE */
  { 0x1028, 0x0626, 0xffff, 0x0009 },  /* Dell -> ALC668_FIXUP_DELL_MIC_NO_PRESENCE */
  { 0x1028, 0x0696, 0xffff, 0x0009 },  /* Dell -> ALC668_FIXUP_DELL_MIC_NO_PRESENCE */
  { 0x1028, 0x0698, 0xffff, 0x0009 },  /* Dell -> ALC668_FIXUP_DELL_MIC_NO_PRESENCE */
  { 0x1028, 0x069f, 0xffff, 0x0009 },  /* Dell -> ALC668_FIXUP_DELL_MIC_NO_PRESENCE */
  { 0x103c, 0x1632, 0xffff, 0x000d },  /* HP RP5800 -> ALC662_FIXUP_HP_RP5800 */
  { 0x103c, 0x870c, 0xffff, 0x000e },  /* HP -> ALC897_FIXUP_HP_HSMIC_VERB */
  { 0x103c, 0x8719, 0xffff, 0x000e },  /* HP -> ALC897_FIXUP_HP_HSMIC_VERB */
  { 0x103c, 0x872b, 0xffff, 0x000e },  /* HP -> ALC897_FIXUP_HP_HSMIC_VERB */
  { 0x103c, 0x873e, 0xffff, 0x7fff },  /* HP -> ALC671_FIXUP_HP_HEADSET_MIC2 (nothing to do) */
  { 0x103c, 0x8768, 0xffff, 0x7fff },  /* HP Slim Desktop S01 -> ALC671_FIXUP_HP_HEADSET_MIC2 (nothing to do) */
  { 0x103c, 0x877e, 0xffff, 0x7fff },  /* HP 288 Pro G6 -> ALC671_FIXUP_HP_HEADSET_MIC2 (nothing to do) */
  { 0x103c, 0x885f, 0xffff, 0x7fff },  /* HP 288 Pro G8 -> ALC671_FIXUP_HP_HEADSET_MIC2 (nothing to do) */
  { 0x1043, 0x1080, 0xffff, 0x7fff },  /* Asus UX501VW -> ALC668_FIXUP_HEADSET_MODE (nothing to do) */
  { 0x1043, 0x11cd, 0xffff, 0x000f },  /* Asus N550 -> ALC662_FIXUP_ASUS_Nx50 */
  { 0x1043, 0x129d, 0xffff, 0x000f },  /* Asus N750 -> ALC662_FIXUP_ASUS_Nx50 */
  { 0x1043, 0x12ff, 0xffff, 0x0012 },  /* ASUS G751 -> ALC668_FIXUP_ASUS_G751 */
  { 0x1043, 0x13df, 0xffff, 0x0010 },  /* Asus N550JX -> ALC662_FIXUP_BASS_1A */
  { 0x1043, 0x1477, 0xffff, 0x0014 },  /* ASUS N56VZ -> ALC662_FIXUP_BASS_MODE4_CHMAP */
  { 0x1043, 0x15a7, 0xffff, 0x0016 },  /* ASUS UX51VZH -> ALC662_FIXUP_BASS_16 */
  { 0x1043, 0x177d, 0xffff, 0x0017 },  /* ASUS N551 -> ALC668_FIXUP_ASUS_Nx51 */
  { 0x1043, 0x17bd, 0xffff, 0x0017 },  /* ASUS N751 -> ALC668_FIXUP_ASUS_Nx51 */
  { 0x1043, 0x185d, 0xffff, 0x0019 },  /* ASUS G551JW -> ALC668_FIXUP_ASUS_NO_HEADSET_MIC */
  { 0x1043, 0x1963, 0xffff, 0x001c },  /* ASUS X71SL -> ALC662_FIXUP_ASUS_MODE8 */
  { 0x1043, 0x1b73, 0xffff, 0x0016 },  /* ASUS N55SF -> ALC662_FIXUP_BASS_16 */
  { 0x1043, 0x1bf3, 0xffff, 0x0014 },  /* ASUS N76VZ -> ALC662_FIXUP_BASS_MODE4_CHMAP */
  { 0x1043, 0x8469, 0xffff, 0x7fff },  /* ASUS mobo -> ALC662_FIXUP_NO_JACK_DETECT (nothing to do) */
  { 0x105b, 0x0cd6, 0xffff, 0x0000 },  /* Foxconn -> ALC662_FIXUP_ASUS_MODE2 */
  { 0x144d, 0xc051, 0xffff, 0x001d },  /* Samsung R720 -> ALC662_FIXUP_IDEAPAD */
  { 0x14cd, 0x5003, 0xffff, 0x001f },  /* USI -> ALC662_FIXUP_USI_HEADSET_MODE */
  { 0x17aa, 0x1036, 0xffff, 0x7fff },  /* Lenovo P520 -> ALC662_FIXUP_LENOVO_MULTI_CODECS (nothing to do) */
  { 0x17aa, 0x1057, 0xffff, 0x0020 },  /* Lenovo P360 -> ALC897_FIXUP_HEADSET_MIC_PIN */
  { 0x17aa, 0x1064, 0xffff, 0x0020 },  /* Lenovo P3 Tower -> ALC897_FIXUP_HEADSET_MIC_PIN */
  { 0x17aa, 0x32ca, 0xffff, 0x0020 },  /* Lenovo ThinkCentre M80 -> ALC897_FIXUP_HEADSET_MIC_PIN */
  { 0x17aa, 0x32cb, 0xffff, 0x0020 },  /* Lenovo ThinkCentre M70 -> ALC897_FIXUP_HEADSET_MIC_PIN */
  { 0x17aa, 0x32cf, 0xffff, 0x0020 },  /* Lenovo ThinkCentre M950 -> ALC897_FIXUP_HEADSET_MIC_PIN */
  { 0x17aa, 0x32f7, 0xffff, 0x0020 },  /* Lenovo ThinkCentre M90 -> ALC897_FIXUP_HEADSET_MIC_PIN */
  { 0x17aa, 0x3321, 0xffff, 0x0020 },  /* Lenovo ThinkCentre M70 Gen4 -> ALC897_FIXUP_HEADSET_MIC_PIN */
  { 0x17aa, 0x331b, 0xffff, 0x0020 },  /* Lenovo ThinkCentre M90 Gen4 -> ALC897_FIXUP_HEADSET_MIC_PIN */
  { 0x17aa, 0x3364, 0xffff, 0x0020 },  /* Lenovo ThinkCentre M90 Gen5 -> ALC897_FIXUP_HEADSET_MIC_PIN */
  { 0x17aa, 0x3742, 0xffff, 0x0022 },  /* Lenovo TianYi510Pro-14IOB -> ALC897_FIXUP_HEADSET_MIC_PIN2 */
  { 0x17aa, 0x38af, 0xffff, 0x001d },  /* Lenovo Ideapad Y550P -> ALC662_FIXUP_IDEAPAD */
  { 0x17aa, 0x3a0d, 0xffff, 0x001d },  /* Lenovo Ideapad Y550 -> ALC662_FIXUP_IDEAPAD */
  { 0x1849, 0x5892, 0xffff, 0x0024 },  /* ASRock B150M -> ALC892_FIXUP_ASROCK_MOBO */
  { 0x19da, 0xa130, 0xffff, 0x0025 },  /* Zotac Z68 -> ALC662_FIXUP_ZOTAC_Z68 */
  { 0x1b0a, 0x01b8, 0xffff, 0x0026 },  /* ACER Veriton -> ALC662_FIXUP_ACER_VERITON */
  { 0x1b35, 0x1234, 0xffff, 0x0027 },  /* CZC ET26 -> ALC662_FIXUP_CZC_ET26 */
  { 0x1b35, 0x2206, 0xffff, 0x0028 },  /* CZC P10T -> ALC662_FIXUP_CZC_P10T */
  { 0x1c6c, 0x1239, 0xffff, 0x000e },  /* Compaq N14JP6-V2 -> ALC897_FIXUP_HP_HSMIC_VERB */
  { 0x1e63, 0x6d9a, 0xffff, 0x000e },  /* F+ FLAPTOP r -> ALC897_FIXUP_HP_HSMIC_VERB */
  { 0x1f66, 0x0202, 0xffff, 0x0029 },  /* Beelink SER6 Max 6900 -> ALC897_FIXUP_BEELINK_SER6_AMP */
};

/* alc880: sound/hda/codecs/realtek/alc880.c  (alc880_fixups + alc880_fixup_tbl) */
static CONST HAK_HDA_CODEC_ID mHakIds_alc880[] = {
  { 0x10ec0880, 0x000000 },  /* ALC880 */
};
static CONST HAK_HDA_PINCFG mHakPins_alc880[] = {
  { 0x17, 0x411111f0 },
  { 0x00, 0x00000000 },
  { 0x14, 0x0121411f },
  { 0x15, 0x411111f0 },
  { 0x16, 0x411111f0 },
  { 0x17, 0x411111f0 },
  { 0x18, 0x90a60160 },
  { 0x19, 0x411111f0 },
  { 0x1a, 0x411111f0 },
  { 0x1b, 0x411111f0 },
  { 0x1c, 0x411111f0 },
  { 0x1d, 0x411111f0 },
  { 0x1e, 0xb743111e },
  { 0x00, 0x00000000 },
  { 0x14, 0x99030120 },
  { 0x15, 0x0121411f },
  { 0x16, 0x411111f0 },
  { 0x17, 0x411111f0 },
  { 0x18, 0x01a19950 },
  { 0x19, 0x411111f0 },
  { 0x1a, 0x01813031 },
  { 0x1b, 0x411111f0 },
  { 0x1c, 0x411111f0 },
  { 0x1d, 0x411111f0 },
  { 0x1e, 0x0144111e },
  { 0x00, 0x00000000 },
  { 0x14, 0x01014010 },
  { 0x15, 0x01016412 },
  { 0x16, 0x01011411 },
  { 0x17, 0x01012414 },
  { 0x18, 0x01a19c30 },
  { 0x19, 0x02a19c40 },
  { 0x1a, 0x01813031 },
  { 0x1b, 0x0121411f },
  { 0x1c, 0x411111f0 },
  { 0x1d, 0x411111f0 },
  { 0x1f, 0x411111f0 },
  { 0x00, 0x00000000 },
  { 0x1b, 0x0121401f },
  { 0x00, 0x00000000 },
  { 0x17, 0x411111f0 },
  { 0x19, 0x411111f0 },
  { 0x1b, 0x411111f0 },
  { 0x1f, 0x411111f0 },
  { 0x00, 0x00000000 },
  { 0x14, 0x0121401f },
  { 0x15, 0x99030120 },
  { 0x16, 0x411111f0 },
  { 0x17, 0x411111f0 },
  { 0x18, 0x411111f0 },
  { 0x19, 0x01a19950 },
  { 0x1a, 0x411111f0 },
  { 0x1b, 0x411111f0 },
  { 0x1c, 0x411111f0 },
  { 0x1d, 0x411111f0 },
  { 0x1e, 0x411111f0 },
  { 0x00, 0x00000000 },
  { 0x14, 0x0121411f },
  { 0x15, 0x99030120 },
  { 0x16, 0x99030130 },
  { 0x00, 0x00000000 },
  { 0x14, 0x0121401f },
  { 0x15, 0x99030120 },
  { 0x16, 0x99030130 },
  { 0x17, 0x411111f0 },
  { 0x18, 0x411111f0 },
  { 0x19, 0x01a19950 },
  { 0x1a, 0x411111f0 },
  { 0x1b, 0x411111f0 },
  { 0x1c, 0x411111f0 },
  { 0x1d, 0x411111f0 },
  { 0x1e, 0x01454140 },
  { 0x00, 0x00000000 },
  { 0x16, 0x411111f0 },
  { 0x18, 0x411111f0 },
  { 0x1a, 0x411111f0 },
  { 0x00, 0x00000000 },
  { 0x1a, 0x0181344f },
  { 0x1b, 0x0321403f },
  { 0x00, 0x00000000 },
  { 0x1e, 0x0144111e },
  { 0x00, 0x00000000 },
  { 0x14, 0x01014010 },
  { 0x15, 0x411111f0 },
  { 0x16, 0x01011411 },
  { 0x17, 0x01016412 },
  { 0x18, 0x01a19c30 },
  { 0x19, 0x0121411f },
  { 0x1a, 0x01813031 },
  { 0x1b, 0x02a19c40 },
  { 0x1c, 0x411111f0 },
  { 0x1d, 0x411111f0 },
  { 0x1f, 0x411111f0 },
  { 0x00, 0x00000000 },
  { 0x1e, 0x411111f0 },
  { 0x00, 0x00000000 },
  { 0x1e, 0x0144111e },
  { 0x00, 0x00000000 },
  { 0x14, 0x01014010 },
  { 0x15, 0x411111f0 },
  { 0x16, 0x411111f0 },
  { 0x17, 0x411111f0 },
  { 0x18, 0x01a19c30 },
  { 0x19, 0x0121411f },
  { 0x1a, 0x01813031 },
  { 0x1b, 0x02a19c40 },
  { 0x1c, 0x411111f0 },
  { 0x1d, 0x411111f0 },
  { 0x1f, 0x411111f0 },
  { 0x00, 0x00000000 },
  { 0x1e, 0x0144111e },
  { 0x00, 0x00000000 },
  { 0x1e, 0x411111f0 },
  { 0x00, 0x00000000 },
  { 0x1e, 0x411111f0 },
  { 0x00, 0x00000000 },
};
static CONST UINT32 mHakVerbs_alc880[] = {
  0x20050007,
  0x20043060,
  0x00000000,
  0x20050007,
  0x20043060,
  0x00000000,
  0x20050007,
  0x20043070,
  0x00000000,
};
static CONST HAK_HDA_FIXUP mHakFixups_alc880[] = {
  { 1, 1, 1, 0, 0x0 },  /* 0 ALC880_FIXUP_W810 */
  { 3, 0, 0, HAK_HDA_FN_GPIO_HIGH, 0x2 },  /* 1 ALC880_FIXUP_GPIO2 alc_fixup_gpio2() */
  { 1, 1, 3, 2, 0x0 },  /* 2 ALC880_FIXUP_ASUS_W5A */
  { 3, 0, 0, HAK_HDA_FN_GPIO_HIGH, 0x1 },  /* 3 ALC880_FIXUP_GPIO1 alc_fixup_gpio1() */
  { 1, 0, 0, 14, 0x0 },  /* 4 ALC880_FIXUP_Z71V */
  { 1, 0, 0, 26, 0x0 },  /* 5 ALC880_FIXUP_6ST_BASE */
  { 1, 2, 5, 38, 0x0 },  /* 6 ALC880_FIXUP_6ST_AUTOMUTE */
  { 2, 0, 0, 0, 0x0 },  /* 7 ALC880_FIXUP_EAPD_COEF */
  { 1, 0, 0, 40, 0x0 },  /* 8 ALC880_FIXUP_UNIWILL_DIG */
  { 1, 1, 10, 45, 0x0 },  /* 9 ALC880_FIXUP_F1734 */
  { 0, 0, 0, 0, 0x0 },  /* 10 ALC880_FIXUP_VOL_KNOB alc880_fixup_vol_knob() NOT PORTED */
  { 1, 0, 0, 57, 0x0 },  /* 11 ALC880_FIXUP_UNIWILL */
  { 2, 1, 1, 3, 0x0 },  /* 12 ALC880_FIXUP_MEDION_RIM */
  { 1, 1, 10, 61, 0x0 },  /* 13 ALC880_FIXUP_FUJITSU */
  { 1, 0, 0, 73, 0x0 },  /* 14 ALC880_FIXUP_LG */
  { 1, 0, 0, 77, 0x0 },  /* 15 ALC880_FIXUP_LG_LW25 */
  { 2, 1, 1, 6, 0x0 },  /* 16 ALC880_FIXUP_TCL_S700 */
  { 1, 1, 18, 80, 0x0 },  /* 17 ALC880_FIXUP_5ST_DIG */
  { 1, 0, 0, 82, 0x0 },  /* 18 ALC880_FIXUP_5ST_BASE */
  { 1, 1, 5, 94, 0x0 },  /* 19 ALC880_FIXUP_6ST */
  { 1, 1, 21, 96, 0x0 },  /* 20 ALC880_FIXUP_3ST_DIG */
  { 1, 0, 0, 98, 0x0 },  /* 21 ALC880_FIXUP_3ST_BASE */
  { 1, 1, 5, 110, 0x0 },  /* 22 ALC880_FIXUP_6ST_DIG */
  { 1, 1, 21, 112, 0x0 },  /* 23 ALC880_FIXUP_3ST */
  { 1, 1, 18, 114, 0x0 },  /* 24 ALC880_FIXUP_5ST */
};
static CONST HAK_HDA_QUIRK mHakQuirks_alc880[] = {
  { 0x1019, 0x0f69, 0xffff, 0x0000 },  /* Coeus G610P -> ALC880_FIXUP_W810 */
  { 0x1043, 0x10c3, 0xffff, 0x0002 },  /* ASUS W5A -> ALC880_FIXUP_ASUS_W5A */
  { 0x1043, 0x1964, 0xffff, 0x0004 },  /* ASUS Z71V -> ALC880_FIXUP_Z71V */
  { 0x1043, 0x0000, 0x0000, 0x0003 },  /* ASUS -> ALC880_FIXUP_GPIO1 */
  { 0x147b, 0x1045, 0xffff, 0x0006 },  /* ABit AA8XE -> ALC880_FIXUP_6ST_AUTOMUTE */
  { 0x1558, 0x5401, 0xffff, 0x0001 },  /* Clevo GPIO2 -> ALC880_FIXUP_GPIO2 */
  { 0x1558, 0x0000, 0x0000, 0x0007 },  /* Clevo -> ALC880_FIXUP_EAPD_COEF */
  { 0x1584, 0x9050, 0xffff, 0x0008 },  /* Uniwill -> ALC880_FIXUP_UNIWILL_DIG */
  { 0x1584, 0x9054, 0xffff, 0x0009 },  /* Uniwill -> ALC880_FIXUP_F1734 */
  { 0x1584, 0x9070, 0xffff, 0x000b },  /* Uniwill -> ALC880_FIXUP_UNIWILL */
  { 0x1584, 0x9077, 0xffff, 0x7fff },  /* Uniwill P53 -> ALC880_FIXUP_VOL_KNOB (nothing to do) */
  { 0x161f, 0x203d, 0xffff, 0x0000 },  /* W810 -> ALC880_FIXUP_W810 */
  { 0x161f, 0x205d, 0xffff, 0x000c },  /* Medion Rim 2150 -> ALC880_FIXUP_MEDION_RIM */
  { 0x1631, 0xe011, 0xffff, 0x0006 },  /* PB 13201056 -> ALC880_FIXUP_6ST_AUTOMUTE */
  { 0x1734, 0x107c, 0xffff, 0x000d },  /* FSC Amilo M1437 -> ALC880_FIXUP_FUJITSU */
  { 0x1734, 0x1094, 0xffff, 0x000d },  /* FSC Amilo M1451G -> ALC880_FIXUP_FUJITSU */
  { 0x1734, 0x10ac, 0xffff, 0x0009 },  /* FSC AMILO Xi 1526 -> ALC880_FIXUP_F1734 */
  { 0x1734, 0x10b0, 0xffff, 0x000d },  /* FSC Amilo Pi1556 -> ALC880_FIXUP_FUJITSU */
  { 0x1854, 0x003b, 0xffff, 0x000e },  /* LG -> ALC880_FIXUP_LG */
  { 0x1854, 0x005f, 0xffff, 0x000e },  /* LG P1 Express -> ALC880_FIXUP_LG */
  { 0x1854, 0x0068, 0xffff, 0x000e },  /* LG w1 -> ALC880_FIXUP_LG */
  { 0x1854, 0x0077, 0xffff, 0x000f },  /* LG LW25 -> ALC880_FIXUP_LG_LW25 */
  { 0x19db, 0x4188, 0xffff, 0x0010 },  /* TCL S700 -> ALC880_FIXUP_TCL_S700 */
  { 0x1019, 0xa880, 0xffff, 0x0011 },  /* ECS -> ALC880_FIXUP_5ST_DIG */
  { 0x1019, 0xa884, 0xffff, 0x0013 },  /* Acer APFV -> ALC880_FIXUP_6ST */
  { 0x1025, 0x0070, 0xffff, 0x0014 },  /* ULI -> ALC880_FIXUP_3ST_DIG */
  { 0x1025, 0x0077, 0xffff, 0x0016 },  /* ULI -> ALC880_FIXUP_6ST_DIG */
  { 0x1025, 0x0078, 0xffff, 0x0016 },  /* ULI -> ALC880_FIXUP_6ST_DIG */
  { 0x1025, 0x0087, 0xffff, 0x0016 },  /* ULI -> ALC880_FIXUP_6ST_DIG */
  { 0x1025, 0xe309, 0xffff, 0x0014 },  /* ULI -> ALC880_FIXUP_3ST_DIG */
  { 0x1025, 0xe310, 0xffff, 0x0017 },  /* ULI -> ALC880_FIXUP_3ST */
  { 0x1039, 0x1234, 0xffff, 0x0016 },  /* NULL -> ALC880_FIXUP_6ST_DIG */
  { 0x104d, 0x81a0, 0xffff, 0x0017 },  /* Sony -> ALC880_FIXUP_3ST */
  { 0x104d, 0x81d6, 0xffff, 0x0017 },  /* Sony -> ALC880_FIXUP_3ST */
  { 0x107b, 0x3032, 0xffff, 0x0018 },  /* Gateway -> ALC880_FIXUP_5ST */
  { 0x107b, 0x3033, 0xffff, 0x0018 },  /* Gateway -> ALC880_FIXUP_5ST */
  { 0x107b, 0x4039, 0xffff, 0x0018 },  /* Gateway -> ALC880_FIXUP_5ST */
  { 0x1297, 0xc790, 0xffff, 0x0016 },  /* Shuttle ST20G5 -> ALC880_FIXUP_6ST_DIG */
  { 0x1458, 0xa102, 0xffff, 0x0016 },  /* Gigabyte K8 -> ALC880_FIXUP_6ST_DIG */
  { 0x1462, 0x1150, 0xffff, 0x0016 },  /* MSI -> ALC880_FIXUP_6ST_DIG */
  { 0x1509, 0x925d, 0xffff, 0x0016 },  /* FIC P4M -> ALC880_FIXUP_6ST_DIG */
  { 0x1565, 0x8202, 0xffff, 0x0011 },  /* Biostar -> ALC880_FIXUP_5ST_DIG */
  { 0x1695, 0x400d, 0xffff, 0x0011 },  /* EPoX -> ALC880_FIXUP_5ST_DIG */
  { 0x1695, 0x4012, 0xffff, 0x0011 },  /* EPox EP-5LDA -> ALC880_FIXUP_5ST_DIG */
  { 0x2668, 0x8086, 0xffff, 0x0016 },  /* NULL -> ALC880_FIXUP_6ST_DIG */
  { 0x8086, 0x2668, 0xffff, 0x0016 },  /* NULL -> ALC880_FIXUP_6ST_DIG */
  { 0x8086, 0xa100, 0xffff, 0x0011 },  /* Intel mobo -> ALC880_FIXUP_5ST_DIG */
  { 0x8086, 0xd400, 0xffff, 0x0011 },  /* Intel mobo -> ALC880_FIXUP_5ST_DIG */
  { 0x8086, 0xd401, 0xffff, 0x0011 },  /* Intel mobo -> ALC880_FIXUP_5ST_DIG */
  { 0x8086, 0xd402, 0xffff, 0x0014 },  /* Intel mobo -> ALC880_FIXUP_3ST_DIG */
  { 0x8086, 0xe224, 0xffff, 0x0011 },  /* Intel mobo -> ALC880_FIXUP_5ST_DIG */
  { 0x8086, 0xe305, 0xffff, 0x0014 },  /* Intel mobo -> ALC880_FIXUP_3ST_DIG */
  { 0x8086, 0xe308, 0xffff, 0x0014 },  /* Intel mobo -> ALC880_FIXUP_3ST_DIG */
  { 0x8086, 0xe400, 0xffff, 0x0011 },  /* Intel mobo -> ALC880_FIXUP_5ST_DIG */
  { 0x8086, 0xe401, 0xffff, 0x0011 },  /* Intel mobo -> ALC880_FIXUP_5ST_DIG */
  { 0x8086, 0xe402, 0xffff, 0x0011 },  /* Intel mobo -> ALC880_FIXUP_5ST_DIG */
  { 0x8086, 0x0000, 0x0000, 0x0017 },  /* Intel mobo -> ALC880_FIXUP_3ST */
  { 0xa0a0, 0x0560, 0xffff, 0x0011 },  /* AOpen i915GMm-HFS -> ALC880_FIXUP_5ST_DIG */
  { 0xe803, 0x1019, 0xffff, 0x0016 },  /* NULL -> ALC880_FIXUP_6ST_DIG */
};

/* alc260: sound/hda/codecs/realtek/alc260.c  (alc260_fixups + alc260_fixup_tbl) */
static CONST HAK_HDA_CODEC_ID mHakIds_alc260[] = {
  { 0x10ec0260, 0x000000 },  /* ALC260 */
};
static CONST HAK_HDA_PINCFG mHakPins_alc260[] = {
  { 0x11, 0x90130110 },
  { 0x00, 0x00000000 },
  { 0x0f, 0x01211020 },
  { 0x10, 0x0001003f },
  { 0x11, 0x411111f0 },
  { 0x12, 0x01a15930 },
  { 0x13, 0x411111f0 },
  { 0x14, 0x411111f0 },
  { 0x15, 0x411111f0 },
  { 0x16, 0x411111f0 },
  { 0x17, 0x411111f0 },
  { 0x18, 0x411111f0 },
  { 0x19, 0x411111f0 },
  { 0x00, 0x00000000 },
  { 0x0f, 0x01214000 },
  { 0x00, 0x00000000 },
};
static CONST UINT32 mHakVerbs_alc260[] = {
  0x1a050007,
  0x1a043040,
  0x00000000,
  0x1a050007,
  0x1a043050,
  0x00000000,
};
static CONST HAK_HDA_FIXUP mHakFixups_alc260[] = {
  { 3, 0, 0, HAK_HDA_FN_GPIO_HIGH, 0x1 },  /* 0 ALC260_FIXUP_GPIO1 alc_fixup_gpio1() */
  { 2, 0, 0, 0, 0x0 },  /* 1 ALC260_FIXUP_COEF */
  { 1, 0, 0, 0, 0x0 },  /* 2 ALC260_FIXUP_HP_DC5750 */
  { 0, 1, 1, 0, 0x0 },  /* 3 ALC260_FIXUP_HP_B1900 alc260_fixup_gpio1_toggle() NOT PORTED */
  { 1, 0, 0, 2, 0x0 },  /* 4 ALC260_FIXUP_VAIO_PINS */
  { 1, 0, 0, 14, 0x0 },  /* 5 ALC260_FIXUP_HP_PIN_0F */
  { 2, 1, 7, 3, 0x0 },  /* 6 ALC260_FIXUP_REPLACER */
  { 0, 1, 5, 0, 0x0 },  /* 7 ALC260_FIXUP_GPIO1_TOGGLE alc260_fixup_gpio1_toggle() NOT PORTED */
};
static CONST HAK_HDA_QUIRK mHakQuirks_alc260[] = {
  { 0x1025, 0x007b, 0xffff, 0x0000 },  /* Acer C20x -> ALC260_FIXUP_GPIO1 */
  { 0x1025, 0x007f, 0xffff, 0x0001 },  /* Acer Aspire 9500 -> ALC260_FIXUP_COEF */
  { 0x1025, 0x008f, 0xffff, 0x0000 },  /* Acer -> ALC260_FIXUP_GPIO1 */
  { 0x103c, 0x280a, 0xffff, 0x0002 },  /* HP dc5750 -> ALC260_FIXUP_HP_DC5750 */
  { 0x103c, 0x30ba, 0xffff, 0x0003 },  /* HP Presario B1900 -> ALC260_FIXUP_HP_B1900 */
  { 0x104d, 0x81bb, 0xffff, 0x0004 },  /* Sony VAIO -> ALC260_FIXUP_VAIO_PINS */
  { 0x104d, 0x81e2, 0xffff, 0x0005 },  /* Sony VAIO TX -> ALC260_FIXUP_HP_PIN_0F */
  { 0x10cf, 0x1326, 0xffff, 0x7fff },  /* FSC LifeBook S7020 -> ALC260_FIXUP_FSC_S7020 (nothing to do) */
  { 0x1509, 0x4540, 0xffff, 0x0000 },  /* Favorit 100XS -> ALC260_FIXUP_GPIO1 */
  { 0x152d, 0x0729, 0xffff, 0x7fff },  /* Quanta KN1 -> ALC260_FIXUP_KN1 (nothing to do) */
  { 0x161f, 0x2057, 0xffff, 0x0006 },  /* Replacer 672V -> ALC260_FIXUP_REPLACER */
  { 0x1631, 0xc017, 0xffff, 0x0001 },  /* PB V7900 -> ALC260_FIXUP_COEF */
};

/* alc262: sound/hda/codecs/realtek/alc262.c  (alc262_fixups + alc262_fixup_tbl) */
static CONST HAK_HDA_CODEC_ID mHakIds_alc262[] = {
  { 0x10ec0262, 0x000000 },  /* ALC262 */
};
static CONST HAK_HDA_PINCFG mHakPins_alc262[] = {
  { 0x16, 0x99130120 },
  { 0x00, 0x00000000 },
  { 0x15, 0x90170110 },
  { 0x00, 0x00000000 },
  { 0x14, 0x1993e1f0 },
  { 0x00, 0x00000000 },
  { 0x14, 0x99130110 },
  { 0x15, 0x0221142f },
  { 0x1b, 0x0121141f },
  { 0x00, 0x00000000 },
  { 0x19, 0x00000021 },
  { 0x00, 0x00000000 },
};
static CONST UINT32 mHakVerbs_alc262[] = {
  0x20050007,
  0x20043070,
  0x00000000,
  0x20050007,
  0x20043050,
  0x00000000,
};
static CONST HAK_HDA_FIXUP mHakFixups_alc262[] = {
  { 1, 0, 0, 0, 0x0 },  /* 0 ALC262_FIXUP_HP_Z200 */
  { 1, 1, 2, 2, 0x0 },  /* 1 ALC262_FIXUP_FSC_S7110 */
  { 2, 0, 0, 0, 0x0 },  /* 2 ALC262_FIXUP_BENQ */
  { 1, 0, 0, 4, 0x0 },  /* 3 ALC262_FIXUP_TYAN */
  { 1, 0, 0, 6, 0x0 },  /* 4 ALC262_FIXUP_FSC_H270 */
  { 4, 1, 2, 10, 0x0 },  /* 5 ALC262_FIXUP_LENOVO_3000 */
  { 2, 0, 0, 3, 0x0 },  /* 6 ALC262_FIXUP_BENQ_T31 */
};
static CONST HAK_HDA_QUIRK mHakQuirks_alc262[] = {
  { 0x103c, 0x170b, 0xffff, 0x0000 },  /* HP Z200 -> ALC262_FIXUP_HP_Z200 */
  { 0x10cf, 0x1397, 0xffff, 0x0001 },  /* Fujitsu Lifebook S7110 -> ALC262_FIXUP_FSC_S7110 */
  { 0x10cf, 0x142d, 0xffff, 0x0002 },  /* Fujitsu Lifebook E8410 -> ALC262_FIXUP_BENQ */
  { 0x10f1, 0x2915, 0xffff, 0x0003 },  /* Tyan Thunder n6650W -> ALC262_FIXUP_TYAN */
  { 0x1734, 0x1141, 0xffff, 0x0004 },  /* FSC ESPRIMO U9210 -> ALC262_FIXUP_FSC_H270 */
  { 0x1734, 0x1147, 0xffff, 0x0004 },  /* FSC Celsius H270 -> ALC262_FIXUP_FSC_H270 */
  { 0x17aa, 0x384e, 0xffff, 0x0005 },  /* Lenovo 3000 -> ALC262_FIXUP_LENOVO_3000 */
  { 0x17ff, 0x0560, 0xffff, 0x0002 },  /* Benq ED8 -> ALC262_FIXUP_BENQ */
  { 0x17ff, 0x058d, 0xffff, 0x0006 },  /* Benq T31-16 -> ALC262_FIXUP_BENQ_T31 */
  { 0x8086, 0x7270, 0xffff, 0x7fff },  /* BayleyBay -> ALC262_FIXUP_INTEL_BAYLEYBAY (nothing to do) */
};

/* alc268: sound/hda/codecs/realtek/alc268.c  (alc268_fixups + alc268_fixup_tbl) */
static CONST HAK_HDA_CODEC_ID mHakIds_alc268[] = {
  { 0x10ec0267, 0x000000 },  /* ALC267 */
  { 0x10ec0268, 0x000000 },  /* ALC268 */
};
static CONST HAK_HDA_PINCFG mHakPins_alc268[] = {
  { 0x1e, 0x014b1180 },
  { 0x00, 0x00000000 },
};
static CONST UINT32 mHakVerbs_alc268[] = {
  0x15070c00,
  0x00000000,
};
static CONST HAK_HDA_FIXUP mHakFixups_alc268[] = {
  { 1, 0, 0, 0, 0x0 },  /* 0 ALC268_FIXUP_SPDIF */
  { 2, 0, 0, 0, 0x0 },  /* 1 ALC268_FIXUP_HP_EAPD */
};
static CONST HAK_HDA_QUIRK mHakQuirks_alc268[] = {
  { 0x1025, 0x0139, 0xffff, 0x0000 },  /* Acer TravelMate 6293 -> ALC268_FIXUP_SPDIF */
  { 0x1025, 0x015b, 0xffff, 0x7fff },  /* Acer AOA 150 (ZG5) -> ALC268_FIXUP_INV_DMIC (nothing to do) */
  { 0x1179, 0xff06, 0xffff, 0x0001 },  /* Toshiba P200 -> ALC268_FIXUP_HP_EAPD */
};

/* alc861: sound/hda/codecs/realtek/alc861.c  (alc861_fixups + alc861_fixup_tbl) */
static CONST HAK_HDA_CODEC_ID mHakIds_alc861[] = {
  { 0x10ec0861, 0x100340 },  /* ALC660 */
  { 0x10ec0861, 0x000000 },  /* ALC861 */
};
static CONST HAK_HDA_PINCFG mHakPins_alc861[] = {
  { 0x0b, 0x0221101f },
  { 0x0f, 0x90170310 },
  { 0x00, 0x00000000 },
};
static CONST UINT32 mHakVerbs_alc861[] = {
  0x10070724,
  0x00000000,
};
static CONST HAK_HDA_FIXUP mHakFixups_alc861[] = {
  { 2, 0, 0, 0, 0x0 },  /* 0 ALC660_FIXUP_ASUS_W7J */
  { 1, 0, 0, 0, 0x0 },  /* 1 ALC861_FIXUP_FSC_AMILO_PI1505 */
};
static CONST HAK_HDA_QUIRK mHakQuirks_alc861[] = {
  { 0x1043, 0x1253, 0xffff, 0x0000 },  /* ASUS W7J -> ALC660_FIXUP_ASUS_W7J */
  { 0x1043, 0x1263, 0xffff, 0x0000 },  /* ASUS Z35HL -> ALC660_FIXUP_ASUS_W7J */
  { 0x1043, 0x1393, 0xffff, 0x7fff },  /* ASUS A6Rp -> ALC861_FIXUP_ASUS_A6RP (nothing to do) */
  { 0x1043, 0x0000, 0x0000, 0x7fff },  /* ASUS laptop -> ALC861_FIXUP_AMP_VREF_0F (nothing to do) */
  { 0x1462, 0x7254, 0xffff, 0x7fff },  /* HP DX2200 -> ALC861_FIXUP_NO_JACK_DETECT (nothing to do) */
  { 0x1584, 0x0000, 0x0000, 0x7fff },  /* Haier/Uniwill -> ALC861_FIXUP_AMP_VREF_0F (nothing to do) */
  { 0x1734, 0x10c7, 0xffff, 0x0001 },  /* FSC Amilo Pi1505 -> ALC861_FIXUP_FSC_AMILO_PI1505 */
};

/* alc861vd: sound/hda/codecs/realtek/alc861vd.c  (alc861vd_fixups + alc861vd_fixup_tbl) */
static CONST HAK_HDA_CODEC_ID mHakIds_alc861vd[] = {
  { 0x10ec0660, 0x000000 },  /* ALC660-VD */
  { 0x10ec0862, 0x000000 },  /* ALC861-VD */
};
static CONST HAK_HDA_QUIRK mHakQuirks_alc861vd[] = {
  { 0x103c, 0x30bf, 0xffff, 0x7fff },  /* HP TX1000 -> ALC861VD_FIX_DALLAS (nothing to do) */
  { 0x1043, 0x1339, 0xffff, 0x7fff },  /* ASUS A7-K -> ALC660VD_FIX_ASUS_GPIO1 (nothing to do) */
  { 0x1179, 0xff31, 0xffff, 0x7fff },  /* Toshiba L30-149 -> ALC861VD_FIX_DALLAS (nothing to do) */
};

/* cxt5045: sound/hda/codecs/conexant.c  (cxt_fixups + cxt5045_fixups) */
static CONST HAK_HDA_CODEC_ID mHakIds_cxt5045[] = {
  { 0x14f15045, 0x000000 },  /*  */
};
static CONST HAK_HDA_PINCFG mHakPins_cxt5045[] = {
  { 0x12, 0x90a60160 },
  { 0x00, 0x00000000 },
  { 0x10, 0x961701f0 },
  { 0x12, 0x02a1901e },
  { 0x14, 0x95a70110 },
  { 0x00, 0x00000000 },
};
static CONST HAK_HDA_FIXUP mHakFixups_cxt5045[] = {
  { 1, 1, 1, 0, 0x0 },  /* 0 CXT_FIXUP_HP_530 */
  { 0, 0, 0, 0, 0x0 },  /* 1 CXT_FIXUP_CAP_MIX_AMP cxt_fixup_cap_mix_amp() NOT PORTED */
  { 1, 0, 0, 2, 0x0 },  /* 2 CXT_FIXUP_TOSHIBA_P105 */
};
static CONST HAK_HDA_QUIRK mHakQuirks_cxt5045[] = {
  { 0x103c, 0x30d5, 0xffff, 0x0000 },  /* HP 530 -> CXT_FIXUP_HP_530 */
  { 0x1179, 0xff31, 0xffff, 0x0002 },  /* Toshiba P105 -> CXT_FIXUP_TOSHIBA_P105 */
  { 0x103c, 0x0000, 0x0000, 0x7fff },  /* HP -> CXT_FIXUP_CAP_MIX_AMP (nothing to do) */
  { 0x1631, 0x0000, 0x0000, 0x7fff },  /* Packard Bell -> CXT_FIXUP_CAP_MIX_AMP (nothing to do) */
  { 0x1734, 0x0000, 0x0000, 0x7fff },  /* Fujitsu -> CXT_FIXUP_CAP_MIX_AMP (nothing to do) */
  { 0x17aa, 0x0000, 0x0000, 0x7fff },  /* Lenovo -> CXT_FIXUP_CAP_MIX_AMP (nothing to do) */
};

/* cxt5047: sound/hda/codecs/conexant.c  (cxt_fixups + cxt5047_fixups) */
static CONST HAK_HDA_CODEC_ID mHakIds_cxt5047[] = {
  { 0x14f15047, 0x000000 },  /*  */
};
static CONST HAK_HDA_QUIRK mHakQuirks_cxt5047[] = {
  { 0x103c, 0x0000, 0x0000, 0x7fff },  /* HP -> CXT_FIXUP_CAP_MIX_AMP_5047 (nothing to do) */
};

/* cxt5051: sound/hda/codecs/conexant.c  (cxt_fixups + cxt5051_fixups) */
static CONST HAK_HDA_CODEC_ID mHakIds_cxt5051[] = {
  { 0x14f15051, 0x000000 },  /*  */
};
static CONST HAK_HDA_PINCFG mHakPins_cxt5051[] = {
  { 0x17, 0x400001f0 },
  { 0x1d, 0x97a70120 },
  { 0x00, 0x00000000 },
  { 0x16, 0x042140ff },
  { 0x17, 0x21a11000 },
  { 0x19, 0x2121103f },
  { 0x1c, 0x21440100 },
  { 0x00, 0x00000000 },
};
static CONST HAK_HDA_FIXUP mHakFixups_cxt5051[] = {
  { 1, 0, 0, 0, 0x0 },  /* 0 CXT_PINCFG_COMPAQ_CQ60 */
  { 1, 0, 0, 3, 0x0 },  /* 1 CXT_PINCFG_LENOVO_X200 */
};
static CONST HAK_HDA_QUIRK mHakQuirks_cxt5051[] = {
  { 0x103c, 0x360b, 0xffff, 0x0000 },  /* Compaq CQ60 -> CXT_PINCFG_COMPAQ_CQ60 */
  { 0x17aa, 0x20f2, 0xffff, 0x0001 },  /* Lenovo X200 -> CXT_PINCFG_LENOVO_X200 */
};

/* cxt5066: sound/hda/codecs/conexant.c  (cxt_fixups + cxt5066_fixups) */
static CONST HAK_HDA_CODEC_ID mHakIds_cxt5066[] = {
  { 0x14f11f86, 0x000000 },  /* CX11880 */
  { 0x14f11f87, 0x000000 },  /* SN6140 */
  { 0x14f12008, 0x000000 },  /* CX8200 */
  { 0x14f120d0, 0x000000 },  /* CX11970 */
  { 0x14f120d1, 0x000000 },  /* SN6180 */
  { 0x14f15066, 0x000000 },  /* CX20582 (Pebble) */
  { 0x14f15067, 0x000000 },  /* CX20583 (Pebble HSF) */
  { 0x14f15068, 0x000000 },  /* CX20584 */
  { 0x14f15069, 0x000000 },  /* CX20585 */
  { 0x14f1506c, 0x000000 },  /* CX20588 */
  { 0x14f1506e, 0x000000 },  /* CX20590 */
  { 0x14f15097, 0x000000 },  /* CX20631 */
  { 0x14f15098, 0x000000 },  /* CX20632 */
  { 0x14f150a1, 0x000000 },  /* CX20641 */
  { 0x14f150a2, 0x000000 },  /* CX20642 */
  { 0x14f150ab, 0x000000 },  /* CX20651 */
  { 0x14f150ac, 0x000000 },  /* CX20652 */
  { 0x14f150b8, 0x000000 },  /* CX20664 */
  { 0x14f150b9, 0x000000 },  /* CX20665 */
  { 0x14f150f1, 0x000000 },  /* CX21722 */
  { 0x14f150f2, 0x000000 },  /* CX20722 */
  { 0x14f150f3, 0x000000 },  /* CX21724 */
  { 0x14f150f4, 0x000000 },  /* CX20724 */
  { 0x14f1510f, 0x000000 },  /* CX20751/2 */
  { 0x14f15110, 0x000000 },  /* CX20751/2 */
  { 0x14f15111, 0x000000 },  /* CX20753/4 */
  { 0x14f15113, 0x000000 },  /* CX20755 */
  { 0x14f15114, 0x000000 },  /* CX20756 */
  { 0x14f15115, 0x000000 },  /* CX20757 */
  { 0x14f151d7, 0x000000 },  /* CX20952 */
};
static CONST HAK_HDA_PINCFG mHakPins_cxt5066[] = {
  { 0x16, 0x0321403f },
  { 0x19, 0x40f001f0 },
  { 0x00, 0x00000000 },
  { 0x16, 0x21011020 },
  { 0x18, 0x2181103f },
  { 0x00, 0x00000000 },
  { 0x1d, 0x91170111 },
  { 0x00, 0x00000000 },
  { 0x1a, 0x02a1113c },
  { 0x00, 0x00000000 },
  { 0x18, 0x03a1913d },
  { 0x00, 0x00000000 },
  { 0x16, 0x03211040 },
  { 0x17, 0x91170110 },
  { 0x18, 0x95a70130 },
  { 0x19, 0x03a11020 },
  { 0x1a, 0x40f001f0 },
  { 0x21, 0x40f001f0 },
  { 0x00, 0x00000000 },
  { 0x19, 0x042110ff },
  { 0x1a, 0x21a190f0 },
  { 0x1c, 0x212140ff },
  { 0x00, 0x00000000 },
  { 0x1a, 0x95a60130 },
  { 0x00, 0x00000000 },
  { 0x18, 0x03211020 },
  { 0x00, 0x00000000 },
  { 0x1a, 0x90a10020 },
  { 0x1b, 0x03a11020 },
  { 0x1d, 0x400101f0 },
  { 0x1e, 0x40a701f0 },
  { 0x20, 0x404501f0 },
  { 0x22, 0x404401f0 },
  { 0x23, 0x40a701f0 },
  { 0x00, 0x00000000 },
  { 0x1a, 0x90a10020 },
  { 0x1b, 0x03a11020 },
  { 0x1d, 0x400101f0 },
  { 0x1e, 0x40a701f0 },
  { 0x20, 0x404501f0 },
  { 0x22, 0x404401f0 },
  { 0x23, 0x40a701f0 },
  { 0x00, 0x00000000 },
  { 0x1d, 0x82170111 },
  { 0x00, 0x00000000 },
};
static CONST UINT32 mHakVerbs_cxt5066[] = {
  0x01071601,
  0x01071701,
  0x01071501,
  0x00000000,
};
static CONST HAK_HDA_FIXUP mHakFixups_cxt5066[] = {
  { 0, 1, 1, 0, 0x0 },  /* 0 CXT_FIXUP_ASPIRE_DMIC cxt_fixup_stereo_dmic() NOT PORTED */
  { 2, 0, 0, 0, 0x0 },  /* 1 CXT_FIXUP_GPIO1 */
  { 1, 0, 0, 0, 0x0 },  /* 2 CXT_FIXUP_ACER_SWIFT_HP */
  { 1, 1, 4, 3, 0x0 },  /* 3 CXT_FIXUP_HP_DOCK */
  { 0, 0, 0, 0, 0x0 },  /* 4 CXT_FIXUP_MUTE_LED_GPIO cxt_fixup_mute_led_gpio() NOT PORTED */
  { 1, 0, 0, 6, 0x0 },  /* 5 CXT_FIXUP_HP_SPECTRE */
  { 1, 1, 7, 8, 0x0 },  /* 6 CXT_FIXUP_HP_MIC_NO_PRESENCE */
  { 0, 0, 0, 0, 0x0 },  /* 7 CXT_FIXUP_HEADSET_MIC cxt_fixup_headset_mic() NOT PORTED */
  { 1, 1, 9, 10, 0x0 },  /* 8 CXT_FIXUP_HEADPHONE_MIC_PIN */
  { 0, 0, 0, 0, 0x0 },  /* 9 CXT_FIXUP_HEADPHONE_MIC cxt_fixup_headphone_mic() NOT PORTED */
  { 1, 0, 0, 12, 0x0 },  /* 10 CXT_PINCFG_SWS_JS201D */
  { 1, 1, 12, 19, 0x0 },  /* 11 CXT_PINCFG_LENOVO_TP410 */
  { 0, 0, 0, 0, 0x0 },  /* 12 CXT_FIXUP_THINKPAD_ACPI hda_fixup_thinkpad_acpi() NOT PORTED */
  { 1, 0, 0, 23, 0x0 },  /* 13 CXT_PINCFG_LENOVO_IDEAPAD_SLIM5_16AKP10 */
  { 1, 0, 0, 25, 0x0 },  /* 14 CXT_FIXUP_HUAWEI_MATEBOOK_HP */
  { 1, 1, 16, 27, 0x0 },  /* 15 CXT_PINCFG_LEMOTE_A1004 */
  { 0, 0, 0, 0, 0x0 },  /* 16 CXT_FIXUP_INC_MIC_BOOST cxt5066_increase_mic_boost() NOT PORTED */
  { 1, 0, 0, 35, 0x0 },  /* 17 CXT_PINCFG_LEMOTE_A1205 */
  { 1, 0, 0, 43, 0x0 },  /* 18 CXT_PINCFG_TOP_SPEAKER */
};
static CONST HAK_HDA_QUIRK mHakQuirks_cxt5066[] = {
  { 0x1025, 0x0543, 0xffff, 0x7fff },  /* Acer Aspire One 522 -> CXT_FIXUP_STEREO_DMIC (nothing to do) */
  { 0x1025, 0x054c, 0xffff, 0x0000 },  /* Acer Aspire 3830TG -> CXT_FIXUP_ASPIRE_DMIC */
  { 0x1025, 0x054f, 0xffff, 0x0000 },  /* Acer Aspire 4830T -> CXT_FIXUP_ASPIRE_DMIC */
  { 0x1025, 0x136d, 0xffff, 0x0002 },  /* Acer Swift SF314 -> CXT_FIXUP_ACER_SWIFT_HP */
  { 0x103c, 0x8079, 0xffff, 0x0003 },  /* HP EliteBook 840 G3 -> CXT_FIXUP_HP_DOCK */
  { 0x103c, 0x807c, 0xffff, 0x0003 },  /* HP EliteBook 820 G3 -> CXT_FIXUP_HP_DOCK */
  { 0x103c, 0x80fd, 0xffff, 0x0003 },  /* HP ProBook 640 G2 -> CXT_FIXUP_HP_DOCK */
  { 0x103c, 0x8115, 0xffff, 0x7fff },  /* HP Z1 Gen3 -> CXT_FIXUP_HP_GATE_MIC (nothing to do) */
  { 0x103c, 0x814f, 0xffff, 0x7fff },  /* HP ZBook 15u G3 -> CXT_FIXUP_MUTE_LED_GPIO (nothing to do) */
  { 0x103c, 0x8174, 0xffff, 0x0005 },  /* HP Spectre x360 -> CXT_FIXUP_HP_SPECTRE */
  { 0x103c, 0x822e, 0xffff, 0x7fff },  /* HP ProBook 440 G4 -> CXT_FIXUP_MUTE_LED_GPIO (nothing to do) */
  { 0x103c, 0x8231, 0xffff, 0x7fff },  /* HP ProBook 450 G4 -> CXT_FIXUP_MUTE_LED_GPIO (nothing to do) */
  { 0x103c, 0x826b, 0xffff, 0x7fff },  /* HP ZBook Studio G4 -> CXT_FIXUP_MUTE_LED_GPIO (nothing to do) */
  { 0x103c, 0x828c, 0xffff, 0x0003 },  /* HP EliteBook 840 G4 -> CXT_FIXUP_HP_DOCK */
  { 0x103c, 0x8299, 0xffff, 0x0006 },  /* HP 800 G3 SFF -> CXT_FIXUP_HP_MIC_NO_PRESENCE */
  { 0x103c, 0x829a, 0xffff, 0x0006 },  /* HP 800 G3 DM -> CXT_FIXUP_HP_MIC_NO_PRESENCE */
  { 0x103c, 0x82b4, 0xffff, 0x0006 },  /* HP ProDesk 600 G3 -> CXT_FIXUP_HP_MIC_NO_PRESENCE */
  { 0x103c, 0x836e, 0xffff, 0x7fff },  /* HP ProBook 455 G5 -> CXT_FIXUP_MUTE_LED_GPIO (nothing to do) */
  { 0x103c, 0x837b, 0xffff, 0x7fff },  /* HP ProBook 440 G5 -> CXT_FIXUP_MUTE_LED_GPIO (nothing to do) */
  { 0x103c, 0x837f, 0xffff, 0x7fff },  /* HP ProBook 470 G5 -> CXT_FIXUP_MUTE_LED_GPIO (nothing to do) */
  { 0x103c, 0x83b2, 0xffff, 0x0003 },  /* HP EliteBook 840 G5 -> CXT_FIXUP_HP_DOCK */
  { 0x103c, 0x83b3, 0xffff, 0x0003 },  /* HP EliteBook 830 G5 -> CXT_FIXUP_HP_DOCK */
  { 0x103c, 0x83d3, 0xffff, 0x0003 },  /* HP ProBook 640 G4 -> CXT_FIXUP_HP_DOCK */
  { 0x103c, 0x83e5, 0xffff, 0x7fff },  /* HP EliteOne 1000 G2 -> CXT_FIXUP_HP_ELITEONE_OUT_DIS (nothing to do) */
  { 0x103c, 0x8402, 0xffff, 0x7fff },  /* HP ProBook 645 G4 -> CXT_FIXUP_MUTE_LED_GPIO (nothing to do) */
  { 0x103c, 0x8427, 0xffff, 0x7fff },  /* HP ZBook Studio G5 -> CXT_FIXUP_HP_ZBOOK_MUTE_LED (nothing to do) */
  { 0x103c, 0x844f, 0xffff, 0x7fff },  /* HP ZBook Studio G5 -> CXT_FIXUP_HP_ZBOOK_MUTE_LED (nothing to do) */
  { 0x103c, 0x8455, 0xffff, 0x0006 },  /* HP Z2 G4 -> CXT_FIXUP_HP_MIC_NO_PRESENCE */
  { 0x103c, 0x8456, 0xffff, 0x0006 },  /* HP Z2 G4 SFF -> CXT_FIXUP_HP_MIC_NO_PRESENCE */
  { 0x103c, 0x8457, 0xffff, 0x0006 },  /* HP Z2 G4 mini -> CXT_FIXUP_HP_MIC_NO_PRESENCE */
  { 0x103c, 0x8458, 0xffff, 0x0006 },  /* HP Z2 G4 mini premium -> CXT_FIXUP_HP_MIC_NO_PRESENCE */
  { 0x1043, 0x138d, 0xffff, 0x0008 },  /* Asus -> CXT_FIXUP_HEADPHONE_MIC_PIN */
  { 0x14f1, 0x0252, 0xffff, 0x7fff },  /* MBX-Z60MR100 -> CXT_FIXUP_HP_A_U (nothing to do) */
  { 0x14f1, 0x0265, 0xffff, 0x000a },  /* SWS JS201D -> CXT_PINCFG_SWS_JS201D */
  { 0x152d, 0x0833, 0xffff, 0x7fff },  /* OLPC XO-1.5 -> CXT_FIXUP_OLPC_XO (nothing to do) */
  { 0x17aa, 0x20f2, 0xffff, 0x000b },  /* Lenovo T400 -> CXT_PINCFG_LENOVO_TP410 */
  { 0x17aa, 0x215e, 0xffff, 0x000b },  /* Lenovo T410 -> CXT_PINCFG_LENOVO_TP410 */
  { 0x17aa, 0x215f, 0xffff, 0x000b },  /* Lenovo T510 -> CXT_PINCFG_LENOVO_TP410 */
  { 0x17aa, 0x21ce, 0xffff, 0x000b },  /* Lenovo T420 -> CXT_PINCFG_LENOVO_TP410 */
  { 0x17aa, 0x21cf, 0xffff, 0x000b },  /* Lenovo T520 -> CXT_PINCFG_LENOVO_TP410 */
  { 0x17aa, 0x21d2, 0xffff, 0x000b },  /* Lenovo T420s -> CXT_PINCFG_LENOVO_TP410 */
  { 0x17aa, 0x21da, 0xffff, 0x000b },  /* Lenovo X220 -> CXT_PINCFG_LENOVO_TP410 */
  { 0x17aa, 0x21db, 0xffff, 0x000b },  /* Lenovo X220-tablet -> CXT_PINCFG_LENOVO_TP410 */
  { 0x17aa, 0x38af, 0xffff, 0x7fff },  /* Lenovo IdeaPad Z560 -> CXT_FIXUP_MUTE_LED_EAPD (nothing to do) */
  { 0x17aa, 0x38b6, 0xffff, 0x000d },  /* Lenovo IdeaPad Slim 5 16AKP10 -> CXT_PINCFG_LENOVO_IDEAPAD_SLIM5_16AKP10 */
  { 0x17aa, 0x3905, 0xffff, 0x7fff },  /* Lenovo G50-30 -> CXT_FIXUP_STEREO_DMIC (nothing to do) */
  { 0x17aa, 0x390b, 0xffff, 0x7fff },  /* Lenovo G50-80 -> CXT_FIXUP_STEREO_DMIC (nothing to do) */
  { 0x17aa, 0x3975, 0xffff, 0x7fff },  /* Lenovo U300s -> CXT_FIXUP_STEREO_DMIC (nothing to do) */
  { 0x17aa, 0x3977, 0xffff, 0x7fff },  /* Lenovo IdeaPad U310 -> CXT_FIXUP_STEREO_DMIC (nothing to do) */
  { 0x17aa, 0x3978, 0xffff, 0x7fff },  /* Lenovo G50-70 -> CXT_FIXUP_STEREO_DMIC (nothing to do) */
  { 0x17aa, 0x397b, 0xffff, 0x7fff },  /* Lenovo S205 -> CXT_FIXUP_STEREO_DMIC (nothing to do) */
  { 0x17aa, 0x0000, 0x0000, 0x7fff },  /* Thinkpad/Ideapad -> CXT_FIXUP_LENOVO_XPAD_ACPI (nothing to do) */
  { 0x19e5, 0x3289, 0xffff, 0x000e },  /* Huawei Matebook -> CXT_FIXUP_HUAWEI_MATEBOOK_HP */
  { 0x1c06, 0x2011, 0xffff, 0x000f },  /* Lemote A1004 -> CXT_PINCFG_LEMOTE_A1004 */
  { 0x1c06, 0x2012, 0xffff, 0x0011 },  /* Lemote A1205 -> CXT_PINCFG_LEMOTE_A1205 */
  { 0x1d05, 0x3012, 0xffff, 0x7fff },  /* MECHREVO Wujie 15X Pro -> CXT_FIXUP_HEADSET_MIC (nothing to do) */
  { 0x2782, 0x12c3, 0xffff, 0x8012 },  /* Sirius Gen1 [codec SSID] -> CXT_PINCFG_TOP_SPEAKER */
  { 0x2782, 0x12c5, 0xffff, 0x8012 },  /* Sirius Gen2 [codec SSID] -> CXT_PINCFG_TOP_SPEAKER */
};

/* sn6186: sound/hda/codecs/senarytech.c  (senary_fixups + sn6186_fixups) */
static CONST HAK_HDA_CODEC_ID mHakIds_sn6186[] = {
  { 0x1fa86186, 0x000000 },  /* SN6186 */
};

/* cs420x: sound/hda/codecs/cirrus/cs420x.c  (cs420x_fixups + cs420x_fixup_tbl) */
static CONST HAK_HDA_CODEC_ID mHakIds_cs420x[] = {
  { 0x10134206, 0x000000 },  /* CS4206 */
  { 0x10134207, 0x000000 },  /* CS4207 */
};
static CONST HAK_HDA_PINCFG mHakPins_cs420x[] = {
  { 0x09, 0x012b4050 },
  { 0x0a, 0x90100141 },
  { 0x0b, 0x90100140 },
  { 0x0c, 0x018b3020 },
  { 0x0d, 0x90a00110 },
  { 0x0e, 0x400000f0 },
  { 0x0f, 0x01cbe030 },
  { 0x10, 0x014be060 },
  { 0x12, 0x400000f0 },
  { 0x15, 0x400000f0 },
  { 0x00, 0x00000000 },
  { 0x09, 0x012b4030 },
  { 0x0a, 0x90100121 },
  { 0x0b, 0x90100120 },
  { 0x0c, 0x400000f0 },
  { 0x0d, 0x90a00110 },
  { 0x0e, 0x400000f0 },
  { 0x0f, 0x400000f0 },
  { 0x10, 0x014be040 },
  { 0x12, 0x400000f0 },
  { 0x15, 0x400000f0 },
  { 0x00, 0x00000000 },
  { 0x0d, 0x40ab90f0 },
  { 0x0e, 0x90a600f0 },
  { 0x12, 0x50a600f0 },
  { 0x00, 0x00000000 },
  { 0x09, 0x012b4030 },
  { 0x0a, 0x400000f0 },
  { 0x0b, 0x90100120 },
  { 0x0c, 0x400000f0 },
  { 0x0d, 0x90a00110 },
  { 0x0e, 0x400000f0 },
  { 0x0f, 0x400000f0 },
  { 0x10, 0x400000f0 },
  { 0x12, 0x400000f0 },
  { 0x15, 0x400000f0 },
  { 0x00, 0x00000000 },
};
static CONST UINT32 mHakVerbs_cs420x[] = {
  0x11050002,
  0x1104102a,
  0x00000000,
};
static CONST HAK_HDA_FIXUP mHakFixups_cs420x[] = {
  { 1, 1, 1, 0, 0x0 },  /* 0 CS420X_MBP53 */
  { 3, 0, 0, HAK_HDA_FN_GPIO_HIGH, 0xa },  /* 1 CS420X_GPIO_13 cs420x_fixup_gpio_13() */
  { 1, 1, 1, 11, 0x0 },  /* 2 CS420X_MBP55 */
  { 3, 0, 0, HAK_HDA_FN_GPIO_HIGH, 0xc },  /* 3 CS420X_GPIO_23 cs420x_fixup_gpio_23() */
  { 2, 1, 1, 0, 0x0 },  /* 4 CS420X_MBP81 */
  { 1, 1, 1, 22, 0x0 },  /* 5 CS420X_MBP101 */
  { 1, 1, 1, 26, 0x0 },  /* 6 CS420X_MBA42 */
};
static CONST HAK_HDA_QUIRK mHakQuirks_cs420x[] = {
  { 0x10de, 0x0ac0, 0xffff, 0x0000 },  /* MacBookPro 5,3 -> CS420X_MBP53 */
  { 0x10de, 0x0d94, 0xffff, 0x0002 },  /* MacBookAir 3,1(2) -> CS420X_MBP55 */
  { 0x10de, 0xcb79, 0xffff, 0x0002 },  /* MacBookPro 5,5 -> CS420X_MBP55 */
  { 0x10de, 0xcb89, 0xffff, 0x0002 },  /* MacBookPro 7,1 -> CS420X_MBP55 */
  { 0x106b, 0x0600, 0xffff, 0x0003 },  /* iMac 14,1 -> CS420X_GPIO_23 */
  { 0x106b, 0x0900, 0xffff, 0x0003 },  /* iMac 12,1 -> CS420X_GPIO_23 */
  { 0x106b, 0x1c00, 0xffff, 0x0004 },  /* MacBookPro 8,1 -> CS420X_MBP81 */
  { 0x106b, 0x2000, 0xffff, 0x0003 },  /* iMac 12,2 -> CS420X_GPIO_23 */
  { 0x106b, 0x2800, 0xffff, 0x0005 },  /* MacBookPro 10,1 -> CS420X_MBP101 */
  { 0x106b, 0x5600, 0xffff, 0x0004 },  /* MacBookAir 5,2 -> CS420X_MBP81 */
  { 0x106b, 0x5b00, 0xffff, 0x0006 },  /* MacBookAir 4,2 -> CS420X_MBA42 */
  { 0x106b, 0x0000, 0x0000, 0x0001 },  /* Apple -> CS420X_GPIO_13 */
};

/* cs4208: sound/hda/codecs/cirrus/cs420x.c  (cs4208_fixups + cs4208_fixup_tbl) */
static CONST HAK_HDA_CODEC_ID mHakIds_cs4208[] = {
  { 0x10134208, 0x000000 },  /* CS4208 */
};
static CONST HAK_HDA_FIXUP mHakFixups_cs4208[] = {
  { 3, 0, 0, HAK_HDA_FN_GPIO_HIGH, 0x1 },  /* 0 CS4208_MAC_AUTO cs4208_fixup_mac() */
};
static CONST HAK_HDA_QUIRK mHakQuirks_cs4208[] = {
  { 0x106b, 0x0000, 0x0000, 0x0000 },  /* Apple -> CS4208_MAC_AUTO */
  { 0x8086, 0x7270, 0xffff, 0x0000 },  /* MacBookAir 7,2 -> CS4208_MAC_AUTO */
};

/* cs421x: sound/hda/codecs/cirrus/cs421x.c  (cs421x_fixups + cs421x_fixup_tbl) */
static CONST HAK_HDA_CODEC_ID mHakIds_cs421x[] = {
  { 0x10134210, 0x000000 },  /* CS4210 */
};
static CONST HAK_HDA_PINCFG mHakPins_cs421x[] = {
  { 0x05, 0x0321401f },
  { 0x06, 0x90170010 },
  { 0x07, 0x03813031 },
  { 0x08, 0xb7a70037 },
  { 0x09, 0xb7a6003e },
  { 0x0a, 0x034510f0 },
  { 0x00, 0x00000000 },
};
static CONST HAK_HDA_FIXUP mHakFixups_cs421x[] = {
  { 1, 1, 1, 0, 0x0 },  /* 0 CS421X_CDB4210 */
  { 0, 0, 0, 0, 0x0 },  /* 1 CS421X_SENSE_B cs421x_fixup_sense_b() NOT PORTED */
};
static CONST HAK_HDA_QUIRK mHakQuirks_cs421x[] = {
  { 0x8086, 0x5001, 0xffff, 0x0000 },  /* DP45SG/CDB4210 -> CS421X_CDB4210 */
};

/* stac9200: sound/hda/codecs/sigmatel.c  (stac9200_fixups + stac9200_fixup_tbl) */
static CONST HAK_HDA_CODEC_ID mHakIds_stac9200[] = {
  { 0x83847690, 0x000000 },  /* STAC9200 */
};
static CONST HAK_HDA_PINCFG mHakPins_stac9200[] = {
  { 0x08, 0x01c47010 },
  { 0x09, 0x01447010 },
  { 0x0d, 0x0221401f },
  { 0x0e, 0x01114010 },
  { 0x0f, 0x02a19020 },
  { 0x10, 0x01a19021 },
  { 0x11, 0x90100140 },
  { 0x12, 0x01813122 },
  { 0x00, 0x00000000 },
  { 0x08, 0x400001f0 },
  { 0x09, 0x400001f1 },
  { 0x0d, 0x02214030 },
  { 0x0e, 0x01014010 },
  { 0x0f, 0x02a19020 },
  { 0x10, 0x01a19021 },
  { 0x11, 0x90100140 },
  { 0x12, 0x01813122 },
  { 0x00, 0x00000000 },
  { 0x08, 0x40c003fa },
  { 0x09, 0x03441340 },
  { 0x0d, 0x0321121f },
  { 0x0e, 0x90170310 },
  { 0x0f, 0x408003fb },
  { 0x10, 0x03a11020 },
  { 0x11, 0x401003fc },
  { 0x12, 0x403003fd },
  { 0x00, 0x00000000 },
  { 0x08, 0x40c003fa },
  { 0x09, 0x01441340 },
  { 0x0d, 0x0421121f },
  { 0x0e, 0x90170310 },
  { 0x0f, 0x408003fb },
  { 0x10, 0x04a11020 },
  { 0x11, 0x401003fc },
  { 0x12, 0x403003fd },
  { 0x00, 0x00000000 },
  { 0x08, 0x400001f0 },
  { 0x09, 0x400001f1 },
  { 0x0d, 0x0221401f },
  { 0x0e, 0x01014010 },
  { 0x0f, 0x01813020 },
  { 0x10, 0x02a19021 },
  { 0x11, 0x90100140 },
  { 0x12, 0x400001f2 },
  { 0x00, 0x00000000 },
  { 0x08, 0x40c003fa },
  { 0x09, 0x0144131f },
  { 0x0d, 0x0321121f },
  { 0x0e, 0x90170310 },
  { 0x0f, 0x90a70321 },
  { 0x10, 0x03a11020 },
  { 0x11, 0x401003fb },
  { 0x12, 0x40f000fc },
  { 0x00, 0x00000000 },
  { 0x08, 0x400001f0 },
  { 0x09, 0x400001f1 },
  { 0x0d, 0x0221401f },
  { 0x0e, 0x01014010 },
  { 0x0f, 0x01813020 },
  { 0x10, 0x01a19021 },
  { 0x11, 0x90100140 },
  { 0x12, 0x400001f2 },
  { 0x00, 0x00000000 },
  { 0x08, 0x40c003fa },
  { 0x09, 0x404003fb },
  { 0x0d, 0x0321121f },
  { 0x0e, 0x90170310 },
  { 0x0f, 0x408003fc },
  { 0x10, 0x03a11020 },
  { 0x11, 0x401003fd },
  { 0x12, 0x403003fe },
  { 0x00, 0x00000000 },
  { 0x08, 0x40c003fa },
  { 0x09, 0x01441340 },
  { 0x0d, 0x0421121f },
  { 0x0e, 0x90170310 },
  { 0x0f, 0x90170310 },
  { 0x10, 0x04a11020 },
  { 0x11, 0x90170310 },
  { 0x12, 0x40f003fc },
  { 0x00, 0x00000000 },
  { 0x08, 0x40c003fa },
  { 0x09, 0x01441340 },
  { 0x0d, 0x0421421f },
  { 0x0e, 0x90170310 },
  { 0x0f, 0x408003fb },
  { 0x10, 0x04a1102e },
  { 0x11, 0x90170311 },
  { 0x12, 0x403003fc },
  { 0x00, 0x00000000 },
  { 0x08, 0x40c003fa },
  { 0x09, 0x404003fb },
  { 0x0d, 0x0421121f },
  { 0x0e, 0x90170310 },
  { 0x0f, 0x408003fc },
  { 0x10, 0x04a11020 },
  { 0x11, 0x401003fd },
  { 0x12, 0x403003fe },
  { 0x00, 0x00000000 },
  { 0x08, 0x400000fe },
  { 0x09, 0x404500f4 },
  { 0x0d, 0x400100f0 },
  { 0x0e, 0x90110010 },
  { 0x0f, 0x400100f1 },
  { 0x10, 0x02a1902e },
  { 0x11, 0x500000f2 },
  { 0x12, 0x500000f3 },
  { 0x00, 0x00000000 },
  { 0x08, 0x400000fe },
  { 0x09, 0x404500f4 },
  { 0x0d, 0x400100f0 },
  { 0x0e, 0x90110010 },
  { 0x0f, 0x400100f1 },
  { 0x10, 0x02a1902e },
  { 0x11, 0x500000f2 },
  { 0x12, 0x500000f3 },
  { 0x00, 0x00000000 },
  { 0x08, 0x40c000f0 },
  { 0x09, 0x404000f1 },
  { 0x0d, 0x0221121f },
  { 0x0e, 0x02211210 },
  { 0x0f, 0x90170111 },
  { 0x10, 0x90a70120 },
  { 0x11, 0x400000f2 },
  { 0x12, 0x400000f3 },
  { 0x00, 0x00000000 },
};
static CONST UINT32 mHakVerbs_stac9200[] = {
  0x08070c02,
  0x00000000,
};
static CONST HAK_HDA_FIXUP mHakFixups_stac9200[] = {
  { 1, 0, 0, 0, 0x0 },  /* 0 STAC_REF */
  { 1, 0, 0, 9, 0x0 },  /* 1 STAC_9200_DELL_D21 */
  { 1, 0, 0, 18, 0x0 },  /* 2 STAC_9200_DELL_M21 */
  { 1, 0, 0, 27, 0x0 },  /* 3 STAC_9200_DELL_M25 */
  { 1, 0, 0, 36, 0x0 },  /* 4 STAC_9200_DELL_D22 */
  { 1, 0, 0, 45, 0x0 },  /* 5 STAC_9200_DELL_M22 */
  { 1, 0, 0, 54, 0x0 },  /* 6 STAC_9200_DELL_D23 */
  { 1, 0, 0, 63, 0x0 },  /* 7 STAC_9200_DELL_M24 */
  { 1, 0, 0, 72, 0x0 },  /* 8 STAC_9200_DELL_M27 */
  { 1, 0, 0, 81, 0x0 },  /* 9 STAC_9200_DELL_M23 */
  { 1, 0, 0, 90, 0x0 },  /* 10 STAC_9200_DELL_M26 */
  { 3, 0, 0, HAK_HDA_FN_GPIO_SET, 0x90900 },  /* 11 STAC_9200_PANASONIC stac9200_fixup_panasonic() */
  { 1, 1, 13, 99, 0x0 },  /* 12 STAC_9200_M4 */
  { 2, 0, 0, 0, 0x0 },  /* 13 STAC_9200_EAPD_INIT */
  { 1, 1, 13, 108, 0x0 },  /* 14 STAC_9200_M4_2 */
  { 1, 1, 13, 117, 0x0 },  /* 15 STAC_9200_OQO */
};
static CONST HAK_HDA_QUIRK mHakQuirks_stac9200[] = {
  { 0x8086, 0x2668, 0xffff, 0x0000 },  /* DFI LanParty -> STAC_REF */
  { 0x15bd, 0x3101, 0xffff, 0x0000 },  /* DFI LanParty -> STAC_REF */
  { 0x1028, 0x01a8, 0xffff, 0x0001 },  /* unknown Dell -> STAC_9200_DELL_D21 */
  { 0x1028, 0x01b5, 0xffff, 0x0002 },  /* Dell Inspiron 630m -> STAC_9200_DELL_M21 */
  { 0x1028, 0x01bd, 0xffff, 0x0003 },  /* Dell Inspiron E1505n -> STAC_9200_DELL_M25 */
  { 0x1028, 0x01c0, 0xffff, 0x0004 },  /* unknown Dell -> STAC_9200_DELL_D22 */
  { 0x1028, 0x01c1, 0xffff, 0x0004 },  /* unknown Dell -> STAC_9200_DELL_D22 */
  { 0x1028, 0x01c2, 0xffff, 0x0005 },  /* Dell Latitude D620 -> STAC_9200_DELL_M22 */
  { 0x1028, 0x01c5, 0xffff, 0x0006 },  /* unknown Dell -> STAC_9200_DELL_D23 */
  { 0x1028, 0x01c7, 0xffff, 0x0006 },  /* unknown Dell -> STAC_9200_DELL_D23 */
  { 0x1028, 0x01c8, 0xffff, 0x0005 },  /* unknown Dell -> STAC_9200_DELL_M22 */
  { 0x1028, 0x01c9, 0xffff, 0x0007 },  /* unknown Dell -> STAC_9200_DELL_M24 */
  { 0x1028, 0x01ca, 0xffff, 0x0007 },  /* unknown Dell -> STAC_9200_DELL_M24 */
  { 0x1028, 0x01cb, 0xffff, 0x0007 },  /* Dell Latitude 120L -> STAC_9200_DELL_M24 */
  { 0x1028, 0x01cc, 0xffff, 0x0005 },  /* Dell Latitude D820 -> STAC_9200_DELL_M22 */
  { 0x1028, 0x01cd, 0xffff, 0x0008 },  /* Dell Inspiron E1705/9400 -> STAC_9200_DELL_M27 */
  { 0x1028, 0x01ce, 0xffff, 0x0009 },  /* Dell XPS M1710 -> STAC_9200_DELL_M23 */
  { 0x1028, 0x01cf, 0xffff, 0x0009 },  /* Dell Precision M90 -> STAC_9200_DELL_M23 */
  { 0x1028, 0x01d3, 0xffff, 0x0005 },  /* unknown Dell -> STAC_9200_DELL_M22 */
  { 0x1028, 0x01d4, 0xffff, 0x0005 },  /* unknown Dell -> STAC_9200_DELL_M22 */
  { 0x1028, 0x01d6, 0xffff, 0x0005 },  /* unknown Dell -> STAC_9200_DELL_M22 */
  { 0x1028, 0x01d8, 0xffff, 0x0002 },  /* Dell Inspiron 640m -> STAC_9200_DELL_M21 */
  { 0x1028, 0x01d9, 0xffff, 0x0006 },  /* unknown Dell -> STAC_9200_DELL_D23 */
  { 0x1028, 0x01da, 0xffff, 0x0006 },  /* unknown Dell -> STAC_9200_DELL_D23 */
  { 0x1028, 0x01de, 0xffff, 0x0001 },  /* unknown Dell -> STAC_9200_DELL_D21 */
  { 0x1028, 0x01e3, 0xffff, 0x0006 },  /* unknown Dell -> STAC_9200_DELL_D23 */
  { 0x1028, 0x01e8, 0xffff, 0x0001 },  /* unknown Dell -> STAC_9200_DELL_D21 */
  { 0x1028, 0x01ee, 0xffff, 0x0003 },  /* unknown Dell -> STAC_9200_DELL_M25 */
  { 0x1028, 0x01ef, 0xffff, 0x0003 },  /* unknown Dell -> STAC_9200_DELL_M25 */
  { 0x1028, 0x01f5, 0xffff, 0x000a },  /* Dell Inspiron 1501 -> STAC_9200_DELL_M26 */
  { 0x1028, 0x01f6, 0xffff, 0x000a },  /* unknown Dell -> STAC_9200_DELL_M26 */
  { 0x1028, 0x0201, 0xffff, 0x0005 },  /* Dell Latitude D430 -> STAC_9200_DELL_M22 */
  { 0x10f7, 0x8338, 0xffff, 0x000b },  /* Panasonic CF-74 -> STAC_9200_PANASONIC */
  { 0x107b, 0x0205, 0xffff, 0x000c },  /* Gateway S-7110M -> STAC_9200_M4 */
  { 0x107b, 0x0317, 0xffff, 0x000e },  /* Gateway MT3423, MX341* -> STAC_9200_M4_2 */
  { 0x107b, 0x0318, 0xffff, 0x000e },  /* Gateway ML3019, MT3707 -> STAC_9200_M4_2 */
  { 0x1106, 0x3288, 0xffff, 0x000f },  /* OQO Model 2 -> STAC_9200_OQO */
};

/* stac925x: sound/hda/codecs/sigmatel.c  (stac925x_fixups + stac925x_fixup_tbl) */
static CONST HAK_HDA_CODEC_ID mHakIds_stac925x[] = {
  { 0x83847632, 0x000000 },  /* STAC9202 */
  { 0x83847633, 0x000000 },  /* STAC9202D */
  { 0x83847634, 0x000000 },  /* STAC9250 */
  { 0x83847635, 0x000000 },  /* STAC9250D */
  { 0x83847636, 0x000000 },  /* STAC9251 */
  { 0x83847637, 0x000000 },  /* STAC9250D */
};
static CONST HAK_HDA_PINCFG mHakPins_stac925x[] = {
  { 0x07, 0x40c003f0 },
  { 0x08, 0x424503f2 },
  { 0x0a, 0x01813022 },
  { 0x0b, 0x02a19021 },
  { 0x0c, 0x90a70320 },
  { 0x0d, 0x02214210 },
  { 0x10, 0x01019020 },
  { 0x11, 0x9033032e },
  { 0x00, 0x00000000 },
  { 0x07, 0x40c003f4 },
  { 0x08, 0x424503f2 },
  { 0x0a, 0x400000f3 },
  { 0x0b, 0x02a19020 },
  { 0x0c, 0x40a000f0 },
  { 0x0d, 0x90100210 },
  { 0x10, 0x400003f1 },
  { 0x11, 0x9033032e },
  { 0x00, 0x00000000 },
  { 0x07, 0x40c003f4 },
  { 0x08, 0x424503f2 },
  { 0x0a, 0x400000f3 },
  { 0x0b, 0x02a19020 },
  { 0x0c, 0x40a000f0 },
  { 0x0d, 0x90100210 },
  { 0x10, 0x400003f1 },
  { 0x11, 0x9033032e },
  { 0x00, 0x00000000 },
  { 0x07, 0x40c003f4 },
  { 0x08, 0x424503f2 },
  { 0x0a, 0x400000f3 },
  { 0x0b, 0x02a19020 },
  { 0x0c, 0x40a000f0 },
  { 0x0d, 0x90100210 },
  { 0x10, 0x400003f1 },
  { 0x11, 0x9033032e },
  { 0x00, 0x00000000 },
  { 0x07, 0x40c003f4 },
  { 0x08, 0x424503f2 },
  { 0x0a, 0x400000f3 },
  { 0x0b, 0x02a19020 },
  { 0x0c, 0x40a000f0 },
  { 0x0d, 0x90100210 },
  { 0x10, 0x400003f1 },
  { 0x11, 0x9033032e },
  { 0x00, 0x00000000 },
  { 0x07, 0x40c003f4 },
  { 0x08, 0x424503f2 },
  { 0x0a, 0x400000f3 },
  { 0x0b, 0x02a19020 },
  { 0x0c, 0x40a000f0 },
  { 0x0d, 0x90100210 },
  { 0x10, 0x400003f1 },
  { 0x11, 0x9033032e },
  { 0x00, 0x00000000 },
  { 0x07, 0x40c003f4 },
  { 0x08, 0x424503f2 },
  { 0x0a, 0x400000f3 },
  { 0x0b, 0x02a19020 },
  { 0x0c, 0x40a000f0 },
  { 0x0d, 0x90100210 },
  { 0x10, 0x400003f1 },
  { 0x11, 0x503303f3 },
  { 0x00, 0x00000000 },
  { 0x07, 0x40c003f4 },
  { 0x08, 0x424503f2 },
  { 0x0a, 0x400000f3 },
  { 0x0b, 0x02a19020 },
  { 0x0c, 0x40a000f0 },
  { 0x0d, 0x90100210 },
  { 0x10, 0x400003f1 },
  { 0x11, 0x90330320 },
  { 0x00, 0x00000000 },
};
static CONST HAK_HDA_FIXUP mHakFixups_stac925x[] = {
  { 1, 0, 0, 0, 0x0 },  /* 0 STAC_REF */
  { 1, 0, 0, 9, 0x0 },  /* 1 STAC_M2_2 */
  { 1, 0, 0, 18, 0x0 },  /* 2 STAC_M2 */
  { 1, 0, 0, 27, 0x0 },  /* 3 STAC_M5 */
  { 1, 0, 0, 36, 0x0 },  /* 4 STAC_M1 */
  { 1, 0, 0, 45, 0x0 },  /* 5 STAC_M1_2 */
  { 1, 0, 0, 54, 0x0 },  /* 6 STAC_M3 */
  { 1, 0, 0, 63, 0x0 },  /* 7 STAC_M6 */
};
static CONST HAK_HDA_QUIRK mHakQuirks_stac925x[] = {
  { 0x8086, 0x2668, 0xffff, 0x0000 },  /* DFI LanParty -> STAC_REF */
  { 0x15bd, 0x3101, 0xffff, 0x0000 },  /* DFI LanParty -> STAC_REF */
  { 0x8384, 0x7632, 0xffff, 0x0000 },  /* Stac9202 Reference Board -> STAC_REF */
  { 0x1002, 0x437b, 0xffff, 0x0001 },  /* Gateway mobile -> STAC_M2_2 */
  { 0x107b, 0x0316, 0xffff, 0x0002 },  /* Gateway M255 -> STAC_M2 */
  { 0x107b, 0x0366, 0xffff, 0x0003 },  /* Gateway MP6954 -> STAC_M5 */
  { 0x107b, 0x0461, 0xffff, 0x0004 },  /* Gateway NX560XL -> STAC_M1 */
  { 0x107b, 0x0681, 0xffff, 0x0002 },  /* Gateway NX860 -> STAC_M2 */
  { 0x107b, 0x0367, 0xffff, 0x0005 },  /* Gateway MX6453 -> STAC_M1_2 */
  { 0x107b, 0x0281, 0xffff, 0x0004 },  /* Gateway mobile -> STAC_M1 */
  { 0x107b, 0x0507, 0xffff, 0x0006 },  /* Gateway mobile -> STAC_M3 */
  { 0x107b, 0x0281, 0xffff, 0x0007 },  /* Gateway mobile -> STAC_M6 */
  { 0x107b, 0x0685, 0xffff, 0x0001 },  /* Gateway mobile -> STAC_M2_2 */
};

/* stac92hd73xx: sound/hda/codecs/sigmatel.c  (stac92hd73xx_fixups + stac92hd73xx_fixup_tbl) */
static CONST HAK_HDA_CODEC_ID mHakIds_stac92hd73xx[] = {
  { 0x111d7674, 0x000000 },  /* 92HD73D1X5 */
  { 0x111d7675, 0x000000 },  /* 92HD73C1X5 */
  { 0x111d7676, 0x000000 },  /* 92HD73E1X5 */
  { 0x111d76c0, 0x000000 },  /* 92HD89C3 */
  { 0x111d76c1, 0x000000 },  /* 92HD89C2 */
  { 0x111d76c2, 0x000000 },  /* 92HD89C1 */
  { 0x111d76c3, 0x000000 },  /* 92HD89B3 */
  { 0x111d76c4, 0x000000 },  /* 92HD89B2 */
  { 0x111d76c5, 0x000000 },  /* 92HD89B1 */
  { 0x111d76c6, 0x000000 },  /* 92HD89E3 */
  { 0x111d76c7, 0x000000 },  /* 92HD89E2 */
  { 0x111d76c8, 0x000000 },  /* 92HD89E1 */
  { 0x111d76c9, 0x000000 },  /* 92HD89D3 */
  { 0x111d76ca, 0x000000 },  /* 92HD89D2 */
  { 0x111d76cb, 0x000000 },  /* 92HD89D1 */
  { 0x111d76cc, 0x000000 },  /* 92HD89F3 */
  { 0x111d76cd, 0x000000 },  /* 92HD89F2 */
  { 0x111d76ce, 0x000000 },  /* 92HD89F1 */
};
static CONST HAK_HDA_PINCFG mHakPins_stac92hd73xx[] = {
  { 0x0a, 0x02214230 },
  { 0x0b, 0x02a19240 },
  { 0x0c, 0x01013214 },
  { 0x0d, 0x01014210 },
  { 0x0e, 0x01a19250 },
  { 0x0f, 0x01011212 },
  { 0x10, 0x01016211 },
  { 0x22, 0x01451380 },
  { 0x23, 0x40f000f0 },
  { 0x00, 0x00000000 },
  { 0x0e, 0x400000f0 },
  { 0x00, 0x00000000 },
  { 0x0a, 0x02214030 },
  { 0x0b, 0x02a19010 },
  { 0x00, 0x00000000 },
  { 0x0c, 0x01014411 },
  { 0x0d, 0x01014410 },
  { 0x0e, 0x01014412 },
  { 0x22, 0x014b1180 },
  { 0x00, 0x00000000 },
};
static CONST HAK_HDA_FIXUP mHakFixups_stac92hd73xx[] = {
  { 3, 0, 0, HAK_HDA_FN_GPIO_SET, 0x0 },  /* 0 STAC_92HD73XX_REF stac92hd73xx_fixup_ref() */
  { 1, 0, 0, 0, 0x0 },  /* 1 STAC_92HD73XX_INTEL */
  { 1, 0, 0, 10, 0x0 },  /* 2 STAC_92HD89XX_HP_Z1_G2_RIGHT_MIC_JACK */
  { 1, 0, 0, 12, 0x0 },  /* 3 STAC_92HD89XX_HP_FRONT_JACK */
  { 1, 0, 0, 15, 0x0 },  /* 4 STAC_92HD73XX_ASUS_MOBO */
};
static CONST HAK_HDA_QUIRK mHakQuirks_stac92hd73xx[] = {
  { 0x8086, 0x2668, 0xffff, 0x0000 },  /* DFI LanParty -> STAC_92HD73XX_REF */
  { 0x15bd, 0x3101, 0xffff, 0x0000 },  /* DFI LanParty -> STAC_92HD73XX_REF */
  { 0x8086, 0x5001, 0xffff, 0x0001 },  /* Intel DP45SG -> STAC_92HD73XX_INTEL */
  { 0x8086, 0x5002, 0xffff, 0x0001 },  /* Intel DG45ID -> STAC_92HD73XX_INTEL */
  { 0x8086, 0x5003, 0xffff, 0x0001 },  /* Intel DG45FC -> STAC_92HD73XX_INTEL */
  { 0x1028, 0x0254, 0xffff, 0x7fff },  /* Dell Studio 1535 -> STAC_DELL_M6_DMIC (nothing to do) */
  { 0x1028, 0x0255, 0xffff, 0x7fff },  /* unknown Dell -> STAC_DELL_M6_DMIC (nothing to do) */
  { 0x1028, 0x0256, 0xffff, 0x7fff },  /* unknown Dell -> STAC_DELL_M6_BOTH (nothing to do) */
  { 0x1028, 0x0257, 0xffff, 0x7fff },  /* unknown Dell -> STAC_DELL_M6_BOTH (nothing to do) */
  { 0x1028, 0x025e, 0xffff, 0x7fff },  /* unknown Dell -> STAC_DELL_M6_AMIC (nothing to do) */
  { 0x1028, 0x025f, 0xffff, 0x7fff },  /* unknown Dell -> STAC_DELL_M6_AMIC (nothing to do) */
  { 0x1028, 0x0271, 0xffff, 0x7fff },  /* unknown Dell -> STAC_DELL_M6_DMIC (nothing to do) */
  { 0x1028, 0x0272, 0xffff, 0x7fff },  /* unknown Dell -> STAC_DELL_M6_DMIC (nothing to do) */
  { 0x1028, 0x029f, 0xffff, 0x7fff },  /* Dell Studio 1537 -> STAC_DELL_M6_DMIC (nothing to do) */
  { 0x1028, 0x02a0, 0xffff, 0x7fff },  /* Dell Studio 17 -> STAC_DELL_M6_DMIC (nothing to do) */
  { 0x1028, 0x02be, 0xffff, 0x7fff },  /* Dell Studio 1555 -> STAC_DELL_M6_DMIC (nothing to do) */
  { 0x1028, 0x02bd, 0xffff, 0x7fff },  /* Dell Studio 1557 -> STAC_DELL_M6_DMIC (nothing to do) */
  { 0x1028, 0x02fe, 0xffff, 0x7fff },  /* Dell Studio XPS 1645 -> STAC_DELL_M6_DMIC (nothing to do) */
  { 0x1028, 0x0413, 0xffff, 0x7fff },  /* Dell Studio 1558 -> STAC_DELL_M6_DMIC (nothing to do) */
  { 0x1028, 0x02a1, 0xffff, 0x7fff },  /* Alienware M17x -> STAC_ALIENWARE_M17X (nothing to do) */
  { 0x1028, 0x043a, 0xffff, 0x7fff },  /* Alienware M17x -> STAC_ALIENWARE_M17X (nothing to do) */
  { 0x1028, 0x0490, 0xffff, 0x7fff },  /* Alienware M17x R3 -> STAC_DELL_EQ (nothing to do) */
  { 0x1059, 0x1011, 0xffff, 0x7fff },  /* ELO VuPoint 15MX -> STAC_ELO_VUPOINT_15MX (nothing to do) */
  { 0x103c, 0x1927, 0xffff, 0x0002 },  /* HP Z1 G2 -> STAC_92HD89XX_HP_Z1_G2_RIGHT_MIC_JACK */
  { 0x103c, 0x2b17, 0xffff, 0x0003 },  /* unknown HP -> STAC_92HD89XX_HP_FRONT_JACK */
  { 0x1043, 0x83f8, 0xffff, 0x0004 },  /* ASUS AT4NM10 -> STAC_92HD73XX_ASUS_MOBO */
};

/* stac92hd83xxx: sound/hda/codecs/sigmatel.c  (stac92hd83xxx_fixups + stac92hd83xxx_fixup_tbl) */
static CONST HAK_HDA_CODEC_ID mHakIds_stac92hd83xxx[] = {
  { 0x111d7604, 0x000000 },  /* 92HD83C1X5 */
  { 0x111d76d4, 0x000000 },  /* 92HD83C1C5 */
  { 0x111d7605, 0x000000 },  /* 92HD81B1X5 */
  { 0x111d76d5, 0x000000 },  /* 92HD81B1C5 */
  { 0x111d76d1, 0x000000 },  /* 92HD87B1/3 */
  { 0x111d76d9, 0x000000 },  /* 92HD87B2/4 */
  { 0x111d7666, 0x000000 },  /* 92HD88B3 */
  { 0x111d7667, 0x000000 },  /* 92HD88B1 */
  { 0x111d7668, 0x000000 },  /* 92HD88B2 */
  { 0x111d7669, 0x000000 },  /* 92HD88B4 */
  { 0x111d76df, 0x000000 },  /* 92HD93BXX */
  { 0x111d76e0, 0x000000 },  /* 92HD91BXX */
  { 0x111d76e3, 0x000000 },  /* 92HD98BXX */
  { 0x111d76e5, 0x000000 },  /* 92HD99BXX */
  { 0x111d76e7, 0x000000 },  /* 92HD90BXX */
  { 0x111d76e8, 0x000000 },  /* 92HD66B1X5 */
  { 0x111d76e9, 0x000000 },  /* 92HD66B2X5 */
  { 0x111d76ea, 0x000000 },  /* 92HD66B3X5 */
  { 0x111d76eb, 0x000000 },  /* 92HD66C1X5 */
  { 0x111d76ec, 0x000000 },  /* 92HD66C2X5 */
  { 0x111d76ed, 0x000000 },  /* 92HD66C3X5 */
  { 0x111d76ee, 0x000000 },  /* 92HD66B1X3 */
  { 0x111d76ef, 0x000000 },  /* 92HD66B2X3 */
  { 0x111d76f0, 0x000000 },  /* 92HD66B3X3 */
  { 0x111d76f1, 0x000000 },  /* 92HD66C1X3 */
  { 0x111d76f2, 0x000000 },  /* 92HD66C2X3 */
  { 0x111d76f3, 0x000000 },  /* 92HD66C3/65 */
};
static CONST HAK_HDA_PINCFG mHakPins_stac92hd83xxx[] = {
  { 0x0a, 0x02214030 },
  { 0x0b, 0x02211010 },
  { 0x0c, 0x02a19020 },
  { 0x0d, 0x02170130 },
  { 0x0e, 0x01014050 },
  { 0x0f, 0x01819040 },
  { 0x10, 0x01014020 },
  { 0x11, 0x90a3014e },
  { 0x1f, 0x01451160 },
  { 0x20, 0x98560170 },
  { 0x00, 0x00000000 },
  { 0x0a, 0x0221403f },
  { 0x0b, 0x0221101f },
  { 0x0c, 0x02a19020 },
  { 0x0d, 0x90170110 },
  { 0x0e, 0x40f000f0 },
  { 0x0f, 0x40f000f0 },
  { 0x10, 0x40f000f0 },
  { 0x11, 0x90a60160 },
  { 0x1f, 0x40f000f0 },
  { 0x20, 0x40f000f0 },
  { 0x00, 0x00000000 },
  { 0x0a, 0x02a11020 },
  { 0x0b, 0x0221101f },
  { 0x0c, 0x400000f0 },
  { 0x0d, 0x90170110 },
  { 0x0e, 0x400000f1 },
  { 0x0f, 0x400000f2 },
  { 0x10, 0x400000f3 },
  { 0x11, 0x90a60160 },
  { 0x1f, 0x400000f4 },
  { 0x20, 0x400000f5 },
  { 0x00, 0x00000000 },
  { 0x0a, 0x40f000f0 },
  { 0x0b, 0x0221101f },
  { 0x0c, 0x02a11020 },
  { 0x0d, 0x92170110 },
  { 0x0e, 0x40f000f0 },
  { 0x0f, 0x92170110 },
  { 0x10, 0x40f000f0 },
  { 0x11, 0xd5a30130 },
  { 0x1f, 0x40f000f0 },
  { 0x20, 0x40f000f0 },
  { 0x00, 0x00000000 },
  { 0x0a, 0x03a12050 },
  { 0x0b, 0x0321201f },
  { 0x0c, 0x40f000f0 },
  { 0x0d, 0x90170110 },
  { 0x0e, 0x40f000f0 },
  { 0x0f, 0x40f000f0 },
  { 0x10, 0x90170110 },
  { 0x11, 0xd5a30140 },
  { 0x1f, 0x40f000f0 },
  { 0x20, 0x40f000f0 },
  { 0x00, 0x00000000 },
  { 0x0f, 0x90170111 },
  { 0x00, 0x00000000 },
  { 0x10, 0x92170111 },
  { 0x00, 0x00000000 },
};
static CONST UINT32 mHakVerbs_stac92hd83xxx[] = {
  0x2207a63e,
  0x2207a768,
  0x2207a817,
  0x2207a93e,
  0x2207aa68,
  0x2207ab17,
  0x2207ac00,
  0x2207ad80,
  0x2207a683,
  0x2207a72f,
  0x2207a8d1,
  0x2207a983,
  0x2207aa2f,
  0x2207abd1,
  0x2207ac01,
  0x2207ad80,
  0x2207a63e,
  0x2207a768,
  0x2207a817,
  0x2207a93e,
  0x2207aa68,
  0x2207ab17,
  0x2207ac02,
  0x2207ad80,
  0x2207a67c,
  0x2207a7c6,
  0x2207a80c,
  0x2207a97c,
  0x2207aac6,
  0x2207ab0c,
  0x2207ac03,
  0x2207ad80,
  0x2207a6c3,
  0x2207a725,
  0x2207a8af,
  0x2207a9c3,
  0x2207aa25,
  0x2207abaf,
  0x2207ac04,
  0x2207ad80,
  0x2207a63e,
  0x2207a785,
  0x2207a873,
  0x2207a93e,
  0x2207aa85,
  0x2207ab73,
  0x2207ac05,
  0x2207ad80,
  0x2207a685,
  0x2207a739,
  0x2207a8c7,
  0x2207a985,
  0x2207aa39,
  0x2207abc7,
  0x2207ac06,
  0x2207ad80,
  0x2207a63c,
  0x2207a790,
  0x2207a8b0,
  0x2207a93c,
  0x2207aa90,
  0x2207abb0,
  0x2207ac07,
  0x2207ad80,
  0x2207a67a,
  0x2207a7c6,
  0x2207a839,
  0x2207a97a,
  0x2207aac6,
  0x2207ab39,
  0x2207ac08,
  0x2207ad80,
  0x2207a6c4,
  0x2207a7e9,
  0x2207a8dc,
  0x2207a9c4,
  0x2207aae9,
  0x2207abdc,
  0x2207ac09,
  0x2207ad80,
  0x2207a63d,
  0x2207a7e1,
  0x2207a80d,
  0x2207a93d,
  0x2207aae1,
  0x2207ab0d,
  0x2207ac0a,
  0x2207ad80,
  0x2207a689,
  0x2207a7b6,
  0x2207a8eb,
  0x2207a989,
  0x2207aab6,
  0x2207abeb,
  0x2207ac0b,
  0x2207ad80,
  0x2207a639,
  0x2207a79d,
  0x2207a8fe,
  0x2207a939,
  0x2207aa9d,
  0x2207abfe,
  0x2207ac0c,
  0x2207ad80,
  0x2207a676,
  0x2207a749,
  0x2207a815,
  0x2207a976,
  0x2207aa49,
  0x2207ab15,
  0x2207ac0d,
  0x2207ad80,
  0x2207a6c8,
  0x2207a780,
  0x2207a8f5,
  0x2207a9c8,
  0x2207aa80,
  0x2207abf5,
  0x2207ac0e,
  0x2207ad80,
  0x2207a640,
  0x2207a700,
  0x2207a800,
  0x2207a940,
  0x2207aa00,
  0x2207ab00,
  0x2207ac0f,
  0x2207ad80,
  0x2207a690,
  0x2207a768,
  0x2207a8f1,
  0x2207a990,
  0x2207aa68,
  0x2207abf1,
  0x2207ac10,
  0x2207ad80,
  0x2207a634,
  0x2207a747,
  0x2207a86c,
  0x2207a934,
  0x2207aa47,
  0x2207ab6c,
  0x2207ac11,
  0x2207ad80,
  0x2207a66f,
  0x2207a797,
  0x2207a80f,
  0x2207a96f,
  0x2207aa97,
  0x2207ab0f,
  0x2207ac12,
  0x2207ad80,
  0x2207a6cb,
  0x2207a7b8,
  0x2207a894,
  0x2207a9cb,
  0x2207aab8,
  0x2207ab94,
  0x2207ac13,
  0x2207ad80,
  0x2207a640,
  0x2207a700,
  0x2207a800,
  0x2207a940,
  0x2207aa00,
  0x2207ab00,
  0x2207ac14,
  0x2207ad80,
  0x2207a695,
  0x2207a776,
  0x2207a85b,
  0x2207a995,
  0x2207aa76,
  0x2207ab5b,
  0x2207ac15,
  0x2207ad80,
  0x2207a631,
  0x2207a7ac,
  0x2207a831,
  0x2207a931,
  0x2207aaac,
  0x2207ab31,
  0x2207ac16,
  0x2207ad80,
  0x2207a66a,
  0x2207a789,
  0x2207a8a5,
  0x2207a96a,
  0x2207aa89,
  0x2207aba5,
  0x2207ac17,
  0x2207ad80,
  0x2207a6ce,
  0x2207a753,
  0x2207a8cf,
  0x2207a9ce,
  0x2207aa53,
  0x2207abcf,
  0x2207ac18,
  0x2207ad80,
  0x2207a640,
  0x2207a700,
  0x2207a800,
  0x2207a940,
  0x2207aa00,
  0x2207ab00,
  0x2207ac19,
  0x2207ad80,
  0x2207a63e,
  0x2207a788,
  0x2207a8dc,
  0x2207a93e,
  0x2207aa88,
  0x2207abdc,
  0x2207ac1a,
  0x2207ad80,
  0x2207a682,
  0x2207a7ee,
  0x2207a846,
  0x2207a982,
  0x2207aaee,
  0x2207ab46,
  0x2207ac1b,
  0x2207ad80,
  0x2207a63e,
  0x2207a788,
  0x2207a8dc,
  0x2207a93e,
  0x2207aa88,
  0x2207abdc,
  0x2207ac1c,
  0x2207ad80,
  0x2207a67d,
  0x2207a709,
  0x2207a828,
  0x2207a97d,
  0x2207aa09,
  0x2207ab28,
  0x2207ac1d,
  0x2207ad80,
  0x2207a6c2,
  0x2207a7e5,
  0x2207a8b4,
  0x2207a9c2,
  0x2207aae5,
  0x2207abb4,
  0x2207ac1e,
  0x2207ad80,
  0x2207a63e,
  0x2207a7a3,
  0x2207a81f,
  0x2207a93e,
  0x2207aaa3,
  0x2207ab1f,
  0x2207ac1f,
  0x2207ad80,
  0x2207a684,
  0x2207a7ca,
  0x2207a8f1,
  0x2207a984,
  0x2207aaca,
  0x2207abf1,
  0x2207ac20,
  0x2207ad80,
  0x2207a63c,
  0x2207a7d5,
  0x2207a89c,
  0x2207a93c,
  0x2207aad5,
  0x2207ab9c,
  0x2207ac21,
  0x2207ad80,
  0x2207a67b,
  0x2207a735,
  0x2207a80f,
  0x2207a97b,
  0x2207aa35,
  0x2207ab0f,
  0x2207ac22,
  0x2207ad80,
  0x2207a6c4,
  0x2207a787,
  0x2207a845,
  0x2207a9c4,
  0x2207aa87,
  0x2207ab45,
  0x2207ac23,
  0x2207ad80,
  0x2207a63e,
  0x2207a70a,
  0x2207a878,
  0x2207a93e,
  0x2207aa0a,
  0x2207ab78,
  0x2207ac24,
  0x2207ad80,
  0x2207a688,
  0x2207a7e2,
  0x2207a805,
  0x2207a988,
  0x2207aae2,
  0x2207ab05,
  0x2207ac25,
  0x2207ad80,
  0x2207a63a,
  0x2207a71a,
  0x2207a8a3,
  0x2207a93a,
  0x2207aa1a,
  0x2207aba3,
  0x2207ac26,
  0x2207ad80,
  0x2207a677,
  0x2207a71d,
  0x2207a8fb,
  0x2207a977,
  0x2207aa1d,
  0x2207abfb,
  0x2207ac27,
  0x2207ad80,
  0x2207a6c7,
  0x2207a7da,
  0x2207a8e5,
  0x2207a9c7,
  0x2207aada,
  0x2207abe5,
  0x2207ac28,
  0x2207ad80,
  0x2207a640,
  0x2207a700,
  0x2207a800,
  0x2207a940,
  0x2207aa00,
  0x2207ab00,
  0x2207ac29,
  0x2207ad80,
  0x2207a68e,
  0x2207a7d7,
  0x2207a822,
  0x2207a98e,
  0x2207aad7,
  0x2207ab22,
  0x2207ac2a,
  0x2207ad80,
  0x2207a635,
  0x2207a726,
  0x2207a8c6,
  0x2207a935,
  0x2207aa26,
  0x2207abc6,
  0x2207ac2b,
  0x2207ad80,
  0x2207a671,
  0x2207a728,
  0x2207a8de,
  0x2207a971,
  0x2207aa28,
  0x2207abde,
  0x2207ac2c,
  0x2207ad80,
  0x2207a6ca,
  0x2207a7d9,
  0x2207a83a,
  0x2207a9ca,
  0x2207aad9,
  0x2207ab3a,
  0x2207ac2d,
  0x2207ad80,
  0x2207a640,
  0x2207a700,
  0x2207a800,
  0x2207a940,
  0x2207aa00,
  0x2207ab00,
  0x2207ac2e,
  0x2207ad80,
  0x2207a693,
  0x2207a75e,
  0x2207a8d8,
  0x2207a993,
  0x2207aa5e,
  0x2207abd8,
  0x2207ac2f,
  0x2207ad80,
  0x2207a632,
  0x2207a7b7,
  0x2207a8b1,
  0x2207a932,
  0x2207aab7,
  0x2207abb1,
  0x2207ac30,
  0x2207ad80,
  0x2207a66c,
  0x2207a7a1,
  0x2207a828,
  0x2207a96c,
  0x2207aaa1,
  0x2207ab28,
  0x2207ac31,
  0x2207ad80,
  0x2207a6cd,
  0x2207a748,
  0x2207a84f,
  0x2207a9cd,
  0x2207aa48,
  0x2207ab4f,
  0x2207ac32,
  0x2207ad80,
  0x2207a640,
  0x2207a700,
  0x2207a800,
  0x2207a940,
  0x2207aa00,
  0x2207ab00,
  0x2207ac33,
  0x2207ad80,
  0x220782c1,
  0x2207712c,
  0x2207722c,
  0x22078804,
  0x0107b008,
  0x00000000,
};
static CONST HAK_HDA_FIXUP mHakFixups_stac92hd83xxx[] = {
  { 1, 0, 0, 0, 0x0 },  /* 0 STAC_92HD83XXX_REF */
  { 1, 0, 0, 11, 0x0 },  /* 1 STAC_DELL_S14 */
  { 1, 0, 0, 22, 0x0 },  /* 2 STAC_DELL_VOSTRO_3500 */
  { 1, 1, 4, 33, 0x0 },  /* 3 STAC_92HD83XXX_HP_cNB11_INTQUAD */
  { 0, 0, 0, 0, 0x0 },  /* 4 STAC_92HD83XXX_HP stac92hd83xxx_fixup_hp() NOT PORTED */
  { 1, 1, 4, 44, 0x0 },  /* 5 STAC_HP_DV7_4000 */
  { 1, 0, 0, 55, 0x0 },  /* 6 STAC_HP_ENVY_BASS */
  { 2, 1, 8, 0, 0x0 },  /* 7 STAC_HP_BNB13_EQ */
  { 0, 1, 4, 0, 0x0 },  /* 8 STAC_92HD83XXX_HP_MIC_LED stac92hd83xxx_fixup_hp_mic_led() NOT PORTED */
  { 1, 0, 0, 57, 0x0 },  /* 9 STAC_HP_ENVY_TS_BASS */
  { 0, 1, 9, 0, 0x0 },  /* 10 STAC_HP_ENVY_TS_DAC_BIND hp_envy_ts_fixup_dac_bind() NOT PORTED */
  { 3, 0, 0, HAK_HDA_FN_GPIO_SET, 0x101010 },  /* 11 STAC_92HD83XXX_GPIO10_EAPD stac92hd83xxx_fixup_gpio10_eapd() */
};
static CONST HAK_HDA_QUIRK mHakQuirks_stac92hd83xxx[] = {
  { 0x8086, 0x2668, 0xffff, 0x0000 },  /* DFI LanParty -> STAC_92HD83XXX_REF */
  { 0x15bd, 0x3101, 0xffff, 0x0000 },  /* DFI LanParty -> STAC_92HD83XXX_REF */
  { 0x1028, 0x02ba, 0xffff, 0x0001 },  /* unknown Dell -> STAC_DELL_S14 */
  { 0x1028, 0x0532, 0xffff, 0x7fff },  /* Dell Latitude E6230 -> STAC_92HD83XXX_HEADSET_JACK (nothing to do) */
  { 0x1028, 0x0533, 0xffff, 0x7fff },  /* Dell Latitude E6330 -> STAC_92HD83XXX_HEADSET_JACK (nothing to do) */
  { 0x1028, 0x0534, 0xffff, 0x7fff },  /* Dell Latitude E6430 -> STAC_92HD83XXX_HEADSET_JACK (nothing to do) */
  { 0x1028, 0x0535, 0xffff, 0x7fff },  /* Dell Latitude E6530 -> STAC_92HD83XXX_HEADSET_JACK (nothing to do) */
  { 0x1028, 0x053c, 0xffff, 0x7fff },  /* Dell Latitude E5430 -> STAC_92HD83XXX_HEADSET_JACK (nothing to do) */
  { 0x1028, 0x053d, 0xffff, 0x7fff },  /* Dell Latitude E5530 -> STAC_92HD83XXX_HEADSET_JACK (nothing to do) */
  { 0x1028, 0x0549, 0xffff, 0x7fff },  /* Dell Latitude E5430 -> STAC_92HD83XXX_HEADSET_JACK (nothing to do) */
  { 0x1028, 0x057d, 0xffff, 0x7fff },  /* Dell Latitude E6430s -> STAC_92HD83XXX_HEADSET_JACK (nothing to do) */
  { 0x1028, 0x0584, 0xffff, 0x7fff },  /* Dell Latitude E6430U -> STAC_92HD83XXX_HEADSET_JACK (nothing to do) */
  { 0x1028, 0x1028, 0xffff, 0x0002 },  /* Dell Vostro 3500 -> STAC_DELL_VOSTRO_3500 */
  { 0x103c, 0x1656, 0xffff, 0x0003 },  /* HP -> STAC_92HD83XXX_HP_cNB11_INTQUAD */
  { 0x103c, 0x1657, 0xffff, 0x0003 },  /* HP -> STAC_92HD83XXX_HP_cNB11_INTQUAD */
  { 0x103c, 0x1658, 0xffff, 0x0003 },  /* HP -> STAC_92HD83XXX_HP_cNB11_INTQUAD */
  { 0x103c, 0x1659, 0xffff, 0x0005 },  /* HP Pavilion dv7 -> STAC_HP_DV7_4000 */
  { 0x103c, 0x165a, 0xffff, 0x0003 },  /* HP -> STAC_92HD83XXX_HP_cNB11_INTQUAD */
  { 0x103c, 0x165b, 0xffff, 0x0003 },  /* HP -> STAC_92HD83XXX_HP_cNB11_INTQUAD */
  { 0x103c, 0x1888, 0xffff, 0x0006 },  /* HP Envy Spectre -> STAC_HP_ENVY_BASS */
  { 0x103c, 0x1899, 0xffff, 0x7fff },  /* HP Folio 13 -> STAC_HP_LED_GPIO10 (nothing to do) */
  { 0x103c, 0x18df, 0xffff, 0x0007 },  /* HP Folio -> STAC_HP_BNB13_EQ */
  { 0x103c, 0x18f8, 0xffff, 0x0007 },  /* HP bNB13 -> STAC_HP_BNB13_EQ */
  { 0x103c, 0x1909, 0xffff, 0x0007 },  /* HP bNB13 -> STAC_HP_BNB13_EQ */
  { 0x103c, 0x190a, 0xffff, 0x0007 },  /* HP bNB13 -> STAC_HP_BNB13_EQ */
  { 0x103c, 0x190e, 0xffff, 0x0009 },  /* HP ENVY TS -> STAC_HP_ENVY_TS_BASS */
  { 0x103c, 0x1967, 0xffff, 0x000a },  /* HP ENVY TS -> STAC_HP_ENVY_TS_DAC_BIND */
  { 0x103c, 0x1940, 0xffff, 0x0007 },  /* HP bNB13 -> STAC_HP_BNB13_EQ */
  { 0x103c, 0x1941, 0xffff, 0x0007 },  /* HP bNB13 -> STAC_HP_BNB13_EQ */
  { 0x103c, 0x1942, 0xffff, 0x0007 },  /* HP bNB13 -> STAC_HP_BNB13_EQ */
  { 0x103c, 0x1943, 0xffff, 0x0007 },  /* HP bNB13 -> STAC_HP_BNB13_EQ */
  { 0x103c, 0x1944, 0xffff, 0x0007 },  /* HP bNB13 -> STAC_HP_BNB13_EQ */
  { 0x103c, 0x1945, 0xffff, 0x0007 },  /* HP bNB13 -> STAC_HP_BNB13_EQ */
  { 0x103c, 0x1946, 0xffff, 0x0007 },  /* HP bNB13 -> STAC_HP_BNB13_EQ */
  { 0x103c, 0x1948, 0xffff, 0x0007 },  /* HP bNB13 -> STAC_HP_BNB13_EQ */
  { 0x103c, 0x1949, 0xffff, 0x0007 },  /* HP bNB13 -> STAC_HP_BNB13_EQ */
  { 0x103c, 0x194a, 0xffff, 0x0007 },  /* HP bNB13 -> STAC_HP_BNB13_EQ */
  { 0x103c, 0x194b, 0xffff, 0x0007 },  /* HP bNB13 -> STAC_HP_BNB13_EQ */
  { 0x103c, 0x194c, 0xffff, 0x0007 },  /* HP bNB13 -> STAC_HP_BNB13_EQ */
  { 0x103c, 0x194e, 0xffff, 0x0007 },  /* HP bNB13 -> STAC_HP_BNB13_EQ */
  { 0x103c, 0x194f, 0xffff, 0x0007 },  /* HP bNB13 -> STAC_HP_BNB13_EQ */
  { 0x103c, 0x1950, 0xffff, 0x0007 },  /* HP bNB13 -> STAC_HP_BNB13_EQ */
  { 0x103c, 0x1951, 0xffff, 0x0007 },  /* HP bNB13 -> STAC_HP_BNB13_EQ */
  { 0x103c, 0x195a, 0xffff, 0x0007 },  /* HP bNB13 -> STAC_HP_BNB13_EQ */
  { 0x103c, 0x195b, 0xffff, 0x0007 },  /* HP bNB13 -> STAC_HP_BNB13_EQ */
  { 0x103c, 0x195c, 0xffff, 0x0007 },  /* HP bNB13 -> STAC_HP_BNB13_EQ */
  { 0x103c, 0x1991, 0xffff, 0x0007 },  /* HP bNB13 -> STAC_HP_BNB13_EQ */
  { 0x103c, 0x2103, 0xffff, 0x0007 },  /* HP bNB13 -> STAC_HP_BNB13_EQ */
  { 0x103c, 0x2104, 0xffff, 0x0007 },  /* HP bNB13 -> STAC_HP_BNB13_EQ */
  { 0x103c, 0x2105, 0xffff, 0x0007 },  /* HP bNB13 -> STAC_HP_BNB13_EQ */
  { 0x103c, 0x2106, 0xffff, 0x0007 },  /* HP bNB13 -> STAC_HP_BNB13_EQ */
  { 0x103c, 0x2107, 0xffff, 0x0007 },  /* HP bNB13 -> STAC_HP_BNB13_EQ */
  { 0x103c, 0x2108, 0xffff, 0x0007 },  /* HP bNB13 -> STAC_HP_BNB13_EQ */
  { 0x103c, 0x2109, 0xffff, 0x0007 },  /* HP bNB13 -> STAC_HP_BNB13_EQ */
  { 0x103c, 0x210a, 0xffff, 0x0007 },  /* HP bNB13 -> STAC_HP_BNB13_EQ */
  { 0x103c, 0x210b, 0xffff, 0x0007 },  /* HP bNB13 -> STAC_HP_BNB13_EQ */
  { 0x103c, 0x211c, 0xffff, 0x0007 },  /* HP bNB13 -> STAC_HP_BNB13_EQ */
  { 0x103c, 0x211d, 0xffff, 0x0007 },  /* HP bNB13 -> STAC_HP_BNB13_EQ */
  { 0x103c, 0x211e, 0xffff, 0x0007 },  /* HP bNB13 -> STAC_HP_BNB13_EQ */
  { 0x103c, 0x211f, 0xffff, 0x0007 },  /* HP bNB13 -> STAC_HP_BNB13_EQ */
  { 0x103c, 0x2120, 0xffff, 0x0007 },  /* HP bNB13 -> STAC_HP_BNB13_EQ */
  { 0x103c, 0x2121, 0xffff, 0x0007 },  /* HP bNB13 -> STAC_HP_BNB13_EQ */
  { 0x103c, 0x2122, 0xffff, 0x0007 },  /* HP bNB13 -> STAC_HP_BNB13_EQ */
  { 0x103c, 0x2123, 0xffff, 0x0007 },  /* HP bNB13 -> STAC_HP_BNB13_EQ */
  { 0x103c, 0x213e, 0xffff, 0x0007 },  /* HP bNB13 -> STAC_HP_BNB13_EQ */
  { 0x103c, 0x213f, 0xffff, 0x0007 },  /* HP bNB13 -> STAC_HP_BNB13_EQ */
  { 0x103c, 0x2140, 0xffff, 0x0007 },  /* HP bNB13 -> STAC_HP_BNB13_EQ */
  { 0x103c, 0x21b2, 0xffff, 0x0007 },  /* HP bNB13 -> STAC_HP_BNB13_EQ */
  { 0x103c, 0x21b3, 0xffff, 0x0007 },  /* HP bNB13 -> STAC_HP_BNB13_EQ */
  { 0x103c, 0x21b5, 0xffff, 0x0007 },  /* HP bNB13 -> STAC_HP_BNB13_EQ */
  { 0x103c, 0x21b6, 0xffff, 0x0007 },  /* HP bNB13 -> STAC_HP_BNB13_EQ */
  { 0x103c, 0x1900, 0xff00, 0x7fff },  /* HP -> STAC_92HD83XXX_HP_MIC_LED (nothing to do) */
  { 0x103c, 0x2000, 0xff00, 0x7fff },  /* HP -> STAC_92HD83XXX_HP_MIC_LED (nothing to do) */
  { 0x103c, 0x2100, 0xff00, 0x7fff },  /* HP -> STAC_92HD83XXX_HP_MIC_LED (nothing to do) */
  { 0x103c, 0x3388, 0xffff, 0x0003 },  /* HP -> STAC_92HD83XXX_HP_cNB11_INTQUAD */
  { 0x103c, 0x3389, 0xffff, 0x0003 },  /* HP -> STAC_92HD83XXX_HP_cNB11_INTQUAD */
  { 0x103c, 0x355b, 0xffff, 0x0003 },  /* HP -> STAC_92HD83XXX_HP_cNB11_INTQUAD */
  { 0x103c, 0x355c, 0xffff, 0x0003 },  /* HP -> STAC_92HD83XXX_HP_cNB11_INTQUAD */
  { 0x103c, 0x355d, 0xffff, 0x0003 },  /* HP -> STAC_92HD83XXX_HP_cNB11_INTQUAD */
  { 0x103c, 0x355e, 0xffff, 0x0003 },  /* HP -> STAC_92HD83XXX_HP_cNB11_INTQUAD */
  { 0x103c, 0x355f, 0xffff, 0x0003 },  /* HP -> STAC_92HD83XXX_HP_cNB11_INTQUAD */
  { 0x103c, 0x3560, 0xffff, 0x0003 },  /* HP -> STAC_92HD83XXX_HP_cNB11_INTQUAD */
  { 0x103c, 0x358b, 0xffff, 0x0003 },  /* HP -> STAC_92HD83XXX_HP_cNB11_INTQUAD */
  { 0x103c, 0x358c, 0xffff, 0x0003 },  /* HP -> STAC_92HD83XXX_HP_cNB11_INTQUAD */
  { 0x103c, 0x358d, 0xffff, 0x0003 },  /* HP -> STAC_92HD83XXX_HP_cNB11_INTQUAD */
  { 0x103c, 0x3591, 0xffff, 0x0003 },  /* HP -> STAC_92HD83XXX_HP_cNB11_INTQUAD */
  { 0x103c, 0x3592, 0xffff, 0x0003 },  /* HP -> STAC_92HD83XXX_HP_cNB11_INTQUAD */
  { 0x103c, 0x3593, 0xffff, 0x0003 },  /* HP -> STAC_92HD83XXX_HP_cNB11_INTQUAD */
  { 0x103c, 0x3561, 0xffff, 0x7fff },  /* HP -> STAC_HP_ZEPHYR (nothing to do) */
  { 0x103c, 0x3660, 0xffff, 0x7fff },  /* HP Mini -> STAC_92HD83XXX_HP_LED (nothing to do) */
  { 0x103c, 0x144e, 0xffff, 0x7fff },  /* HP Pavilion dv5 -> STAC_92HD83XXX_HP_INV_LED (nothing to do) */
  { 0x103c, 0x148a, 0xffff, 0x7fff },  /* HP Mini -> STAC_92HD83XXX_HP_LED (nothing to do) */
  { 0x103c, 0x0000, 0x0000, 0x7fff },  /* HP -> STAC_92HD83XXX_HP (nothing to do) */
  { 0x1179, 0xfa91, 0xfffd, 0x000b },  /* Toshiba Satellite S50D -> STAC_92HD83XXX_GPIO10_EAPD */
};

/* stac92hd71bxx: sound/hda/codecs/sigmatel.c  (stac92hd71bxx_fixups + stac92hd71bxx_fixup_tbl) */
static CONST HAK_HDA_CODEC_ID mHakIds_stac92hd71bxx[] = {
  { 0x111d7603, 0x000000 },  /* 92HD75B3X5 */
  { 0x111d7608, 0x000000 },  /* 92HD75B2X5 */
  { 0x111d76b0, 0x000000 },  /* 92HD71B8X */
  { 0x111d76b1, 0x000000 },  /* 92HD71B8X */
  { 0x111d76b2, 0x000000 },  /* 92HD71B7X */
  { 0x111d76b3, 0x000000 },  /* 92HD71B7X */
  { 0x111d76b4, 0x000000 },  /* 92HD71B6X */
  { 0x111d76b5, 0x000000 },  /* 92HD71B6X */
  { 0x111d76b6, 0x000000 },  /* 92HD71B5X */
  { 0x111d76b7, 0x000000 },  /* 92HD71B5X */
};
static CONST HAK_HDA_PINCFG mHakPins_stac92hd71bxx[] = {
  { 0x0a, 0x0421101f },
  { 0x0b, 0x04a11221 },
  { 0x0c, 0x40f000f0 },
  { 0x0d, 0x90170110 },
  { 0x0e, 0x23a1902e },
  { 0x0f, 0x23014250 },
  { 0x14, 0x40f000f0 },
  { 0x18, 0x90a000f0 },
  { 0x19, 0x40f000f0 },
  { 0x1e, 0x4f0000f0 },
  { 0x1f, 0x4f0000f0 },
  { 0x00, 0x00000000 },
  { 0x0a, 0x0421101f },
  { 0x0b, 0x04a11221 },
  { 0x0c, 0x90a70330 },
  { 0x0d, 0x90170110 },
  { 0x0e, 0x23a1902e },
  { 0x0f, 0x23014250 },
  { 0x14, 0x40f000f0 },
  { 0x18, 0x40f000f0 },
  { 0x19, 0x40f000f0 },
  { 0x1e, 0x044413b0 },
  { 0x1f, 0x044413b0 },
  { 0x00, 0x00000000 },
  { 0x0a, 0x0421101f },
  { 0x0b, 0x04a11221 },
  { 0x0c, 0x90a70330 },
  { 0x0d, 0x90170110 },
  { 0x0e, 0x40f000f0 },
  { 0x0f, 0x40f000f0 },
  { 0x14, 0x40f000f0 },
  { 0x18, 0x90a000f0 },
  { 0x19, 0x40f000f0 },
  { 0x1e, 0x044413b0 },
  { 0x1f, 0x044413b0 },
  { 0x00, 0x00000000 },
};
static CONST HAK_HDA_FIXUP mHakFixups_stac92hd71bxx[] = {
  { 3, 0, 0, HAK_HDA_FN_GPIO_SET, 0x0 },  /* 0 STAC_92HD71BXX_REF stac92hd71bxx_fixup_ref() */
  { 1, 0, 0, 0, 0x0 },  /* 1 STAC_DELL_M4_1 */
  { 1, 0, 0, 12, 0x0 },  /* 2 STAC_DELL_M4_2 */
  { 1, 0, 0, 24, 0x0 },  /* 3 STAC_DELL_M4_3 */
};
static CONST HAK_HDA_QUIRK mHakQuirks_stac92hd71bxx[] = {
  { 0x8086, 0x2668, 0xffff, 0x0000 },  /* DFI LanParty -> STAC_92HD71BXX_REF */
  { 0x15bd, 0x3101, 0xffff, 0x0000 },  /* DFI LanParty -> STAC_92HD71BXX_REF */
  { 0x103c, 0x1720, 0xfff0, 0x7fff },  /* HP -> STAC_HP_DV5 (nothing to do) */
  { 0x103c, 0x3080, 0xfff0, 0x7fff },  /* HP -> STAC_HP_DV5 (nothing to do) */
  { 0x103c, 0x30f0, 0xfff0, 0x7fff },  /* HP dv4-7 -> STAC_HP_DV4 (nothing to do) */
  { 0x103c, 0x3600, 0xfff0, 0x7fff },  /* HP dv4-7 -> STAC_HP_DV5 (nothing to do) */
  { 0x103c, 0x3610, 0xffff, 0x7fff },  /* HP HDX -> STAC_HP_HDX (nothing to do) */
  { 0x103c, 0x361a, 0xffff, 0x7fff },  /* HP mini 1000 -> STAC_HP_M4 (nothing to do) */
  { 0x103c, 0x361b, 0xffff, 0x7fff },  /* HP HDX -> STAC_HP_HDX (nothing to do) */
  { 0x103c, 0x3620, 0xfff0, 0x7fff },  /* HP dv6 -> STAC_HP_DV5 (nothing to do) */
  { 0x103c, 0x3061, 0xffff, 0x7fff },  /* HP dv6 -> STAC_HP_DV5 (nothing to do) */
  { 0x103c, 0x363e, 0xffff, 0x7fff },  /* HP DV6 -> STAC_HP_DV5 (nothing to do) */
  { 0x103c, 0x7010, 0xfff0, 0x7fff },  /* HP -> STAC_HP_DV5 (nothing to do) */
  { 0x103c, 0x0000, 0x0000, 0x7fff },  /* HP -> STAC_92HD71BXX_HP (nothing to do) */
  { 0x1028, 0x0233, 0xffff, 0x0001 },  /* unknown Dell -> STAC_DELL_M4_1 */
  { 0x1028, 0x0234, 0xffff, 0x0001 },  /* unknown Dell -> STAC_DELL_M4_1 */
  { 0x1028, 0x0250, 0xffff, 0x0001 },  /* unknown Dell -> STAC_DELL_M4_1 */
  { 0x1028, 0x024f, 0xffff, 0x0001 },  /* unknown Dell -> STAC_DELL_M4_1 */
  { 0x1028, 0x024d, 0xffff, 0x0001 },  /* unknown Dell -> STAC_DELL_M4_1 */
  { 0x1028, 0x0251, 0xffff, 0x0001 },  /* unknown Dell -> STAC_DELL_M4_1 */
  { 0x1028, 0x0277, 0xffff, 0x0001 },  /* unknown Dell -> STAC_DELL_M4_1 */
  { 0x1028, 0x0263, 0xffff, 0x0002 },  /* unknown Dell -> STAC_DELL_M4_2 */
  { 0x1028, 0x0265, 0xffff, 0x0002 },  /* unknown Dell -> STAC_DELL_M4_2 */
  { 0x1028, 0x0262, 0xffff, 0x0002 },  /* unknown Dell -> STAC_DELL_M4_2 */
  { 0x1028, 0x0264, 0xffff, 0x0002 },  /* unknown Dell -> STAC_DELL_M4_2 */
  { 0x1028, 0x02aa, 0xffff, 0x0003 },  /* unknown Dell -> STAC_DELL_M4_3 */
};

/* stac922x: sound/hda/codecs/sigmatel.c  (stac922x_fixups + stac922x_fixup_tbl) */
static CONST HAK_HDA_CODEC_ID mHakIds_stac922x[] = {
  { 0x83847882, 0x000000 },  /* STAC9220 A1 */
  { 0x83847680, 0x000000 },  /* STAC9221 A1 */
  { 0x83847880, 0x000000 },  /* STAC9220 A2 */
  { 0x83847681, 0x000000 },  /* STAC9220D/9223D A2 */
  { 0x83847682, 0x000000 },  /* STAC9221 A2 */
  { 0x83847683, 0x000000 },  /* STAC9221D A2 */
};
static CONST HAK_HDA_PINCFG mHakPins_stac922x[] = {
  { 0x0a, 0x01014010 },
  { 0x0b, 0x01016011 },
  { 0x0c, 0x01012012 },
  { 0x0d, 0x0221401f },
  { 0x0e, 0x01813122 },
  { 0x0f, 0x01011014 },
  { 0x10, 0x01441030 },
  { 0x11, 0x01c41030 },
  { 0x15, 0x40000100 },
  { 0x1b, 0x40000100 },
  { 0x00, 0x00000000 },
  { 0x0a, 0x0221401f },
  { 0x0b, 0x01a19022 },
  { 0x0c, 0x01813021 },
  { 0x0d, 0x01014010 },
  { 0x0e, 0x40000100 },
  { 0x0f, 0x40000100 },
  { 0x10, 0x40000100 },
  { 0x11, 0x40000100 },
  { 0x15, 0x02a19120 },
  { 0x1b, 0x40000100 },
  { 0x00, 0x00000000 },
  { 0x0a, 0x0221401f },
  { 0x0b, 0x01011012 },
  { 0x0c, 0x01813024 },
  { 0x0d, 0x01014010 },
  { 0x0e, 0x01a19021 },
  { 0x0f, 0x01016011 },
  { 0x10, 0x01452130 },
  { 0x11, 0x40000100 },
  { 0x15, 0x02a19320 },
  { 0x1b, 0x40000100 },
  { 0x00, 0x00000000 },
  { 0x0a, 0x02214030 },
  { 0x0b, 0x01a19021 },
  { 0x0c, 0x01111012 },
  { 0x0d, 0x01114010 },
  { 0x0e, 0x02a19020 },
  { 0x0f, 0x01117011 },
  { 0x10, 0x400001f0 },
  { 0x11, 0x400001f1 },
  { 0x15, 0x01813122 },
  { 0x1b, 0x400001f2 },
  { 0x00, 0x00000000 },
  { 0x0a, 0x02214030 },
  { 0x0b, 0x01a19021 },
  { 0x0c, 0x01111012 },
  { 0x0d, 0x01114010 },
  { 0x0e, 0x02a19020 },
  { 0x0f, 0x01117011 },
  { 0x10, 0x01451140 },
  { 0x11, 0x400001f0 },
  { 0x15, 0x01813122 },
  { 0x1b, 0x400001f1 },
  { 0x00, 0x00000000 },
  { 0x0a, 0x0321101f },
  { 0x0b, 0x01112024 },
  { 0x0c, 0x01111222 },
  { 0x0d, 0x91174220 },
  { 0x0e, 0x03a11050 },
  { 0x0f, 0x01116221 },
  { 0x10, 0x90a70330 },
  { 0x11, 0x01452340 },
  { 0x15, 0x40c003f1 },
  { 0x1b, 0x405003f0 },
  { 0x00, 0x00000000 },
  { 0x0a, 0x02211211 },
  { 0x0b, 0x408103ff },
  { 0x0c, 0x02a1123e },
  { 0x0d, 0x90100310 },
  { 0x0e, 0x408003f1 },
  { 0x0f, 0x0221121f },
  { 0x10, 0x03451340 },
  { 0x11, 0x40c003f2 },
  { 0x15, 0x508003f3 },
  { 0x1b, 0x405003f4 },
  { 0x00, 0x00000000 },
  { 0x0a, 0x0221401f },
  { 0x0b, 0x02a19020 },
  { 0x0c, 0x01a19020 },
  { 0x0d, 0x01114010 },
  { 0x0e, 0x408000f0 },
  { 0x0f, 0x01813022 },
  { 0x10, 0x074510a0 },
  { 0x11, 0x40c400f1 },
  { 0x15, 0x9037012e },
  { 0x1b, 0x40e000f2 },
  { 0x00, 0x00000000 },
};
static CONST HAK_HDA_FIXUP mHakFixups_stac922x[] = {
  { 1, 0, 0, 0, 0x0 },  /* 0 STAC_D945_REF */
  { 1, 0, 0, 11, 0x0 },  /* 1 STAC_D945GTP3 */
  { 1, 0, 0, 22, 0x0 },  /* 2 STAC_D945GTP5 */
  { 1, 0, 0, 33, 0x0 },  /* 3 STAC_922X_DELL_D81 */
  { 1, 0, 0, 44, 0x0 },  /* 4 STAC_922X_DELL_D82 */
  { 1, 0, 0, 55, 0x0 },  /* 5 STAC_922X_DELL_M81 */
  { 1, 0, 0, 66, 0x0 },  /* 6 STAC_922X_DELL_M82 */
  { 1, 0, 0, 77, 0x0 },  /* 7 STAC_ECS_202 */
};
static CONST HAK_HDA_QUIRK mHakQuirks_stac922x[] = {
  { 0x8086, 0x2668, 0xffff, 0x0000 },  /* DFI LanParty -> STAC_D945_REF */
  { 0x15bd, 0x3101, 0xffff, 0x0000 },  /* DFI LanParty -> STAC_D945_REF */
  { 0x8086, 0x0101, 0xffff, 0x0001 },  /* Intel D945G -> STAC_D945GTP3 */
  { 0x8086, 0x0202, 0xffff, 0x0001 },  /* Intel D945G -> STAC_D945GTP3 */
  { 0x8086, 0x0606, 0xffff, 0x0001 },  /* Intel D945G -> STAC_D945GTP3 */
  { 0x8086, 0x0601, 0xffff, 0x0001 },  /* Intel D945G -> STAC_D945GTP3 */
  { 0x8086, 0x0111, 0xffff, 0x0001 },  /* Intel D945G -> STAC_D945GTP3 */
  { 0x8086, 0x1115, 0xffff, 0x0001 },  /* Intel D945G -> STAC_D945GTP3 */
  { 0x8086, 0x1116, 0xffff, 0x0001 },  /* Intel D945G -> STAC_D945GTP3 */
  { 0x8086, 0x1117, 0xffff, 0x0001 },  /* Intel D945G -> STAC_D945GTP3 */
  { 0x8086, 0x1118, 0xffff, 0x0001 },  /* Intel D945G -> STAC_D945GTP3 */
  { 0x8086, 0x1119, 0xffff, 0x0001 },  /* Intel D945G -> STAC_D945GTP3 */
  { 0x8086, 0x8826, 0xffff, 0x0001 },  /* Intel D945G -> STAC_D945GTP3 */
  { 0x8086, 0x5049, 0xffff, 0x0001 },  /* Intel D945G -> STAC_D945GTP3 */
  { 0x8086, 0x5055, 0xffff, 0x0001 },  /* Intel D945G -> STAC_D945GTP3 */
  { 0x8086, 0x5048, 0xffff, 0x0001 },  /* Intel D945G -> STAC_D945GTP3 */
  { 0x8086, 0x0110, 0xffff, 0x0001 },  /* Intel D945G -> STAC_D945GTP3 */
  { 0x8086, 0x0404, 0xffff, 0x0002 },  /* Intel D945G -> STAC_D945GTP5 */
  { 0x8086, 0x0303, 0xffff, 0x0002 },  /* Intel D945G -> STAC_D945GTP5 */
  { 0x8086, 0x0013, 0xffff, 0x0002 },  /* Intel D945G -> STAC_D945GTP5 */
  { 0x8086, 0x0417, 0xffff, 0x0002 },  /* Intel D945G -> STAC_D945GTP5 */
  { 0x8086, 0x0b0b, 0xffff, 0x0001 },  /* Intel D945P -> STAC_D945GTP3 */
  { 0x8086, 0x0112, 0xffff, 0x0001 },  /* Intel D945P -> STAC_D945GTP3 */
  { 0x8086, 0x0d0d, 0xffff, 0x0001 },  /* Intel D945P -> STAC_D945GTP3 */
  { 0x8086, 0x0909, 0xffff, 0x0001 },  /* Intel D945P -> STAC_D945GTP3 */
  { 0x8086, 0x0505, 0xffff, 0x0001 },  /* Intel D945P -> STAC_D945GTP3 */
  { 0x8086, 0x0707, 0xffff, 0x0002 },  /* Intel D945P -> STAC_D945GTP5 */
  { 0x8086, 0x0204, 0xffff, 0x0000 },  /* Intel D945 -> STAC_D945_REF */
  { 0x8384, 0x7680, 0xffff, 0x7fff },  /* Mac -> STAC_INTEL_MAC_AUTO (nothing to do) */
  { 0x1028, 0x01a7, 0xffff, 0x0003 },  /* unknown Dell -> STAC_922X_DELL_D81 */
  { 0x1028, 0x01a9, 0xffff, 0x0003 },  /* unknown Dell -> STAC_922X_DELL_D81 */
  { 0x1028, 0x01ab, 0xffff, 0x0003 },  /* unknown Dell -> STAC_922X_DELL_D81 */
  { 0x1028, 0x01ac, 0xffff, 0x0004 },  /* unknown Dell -> STAC_922X_DELL_D82 */
  { 0x1028, 0x01bf, 0xffff, 0x0005 },  /* unknown Dell -> STAC_922X_DELL_M81 */
  { 0x1028, 0x01d0, 0xffff, 0x0004 },  /* unknown Dell -> STAC_922X_DELL_D82 */
  { 0x1028, 0x01d1, 0xffff, 0x0003 },  /* unknown Dell -> STAC_922X_DELL_D81 */
  { 0x1028, 0x01d2, 0xffff, 0x0003 },  /* unknown Dell -> STAC_922X_DELL_D81 */
  { 0x1028, 0x01d7, 0xffff, 0x0006 },  /* Dell XPS M1210 -> STAC_922X_DELL_M82 */
  { 0x1019, 0x2000, 0xf000, 0x0007 },  /* ECS/PC chips -> STAC_ECS_202 */
};

/* stac927x: sound/hda/codecs/sigmatel.c  (stac927x_fixups + stac927x_fixup_tbl) */
static CONST HAK_HDA_CODEC_ID mHakIds_stac927x[] = {
  { 0x83847618, 0x000000 },  /* STAC9227 */
  { 0x83847619, 0x000000 },  /* STAC9227 */
  { 0x83847638, 0x000000 },  /* STAC92HD700 */
  { 0x83847616, 0x000000 },  /* STAC9228 */
  { 0x83847617, 0x000000 },  /* STAC9228 */
  { 0x83847614, 0x000000 },  /* STAC9229 */
  { 0x83847615, 0x000000 },  /* STAC9229 */
  { 0x83847620, 0x000000 },  /* STAC9274 */
  { 0x83847621, 0x000000 },  /* STAC9274D */
  { 0x83847622, 0x000000 },  /* STAC9273X */
  { 0x83847623, 0x000000 },  /* STAC9273D */
  { 0x83847624, 0x000000 },  /* STAC9272X */
  { 0x83847625, 0x000000 },  /* STAC9272D */
  { 0x83847626, 0x000000 },  /* STAC9271X */
  { 0x83847627, 0x000000 },  /* STAC9271D */
  { 0x83847628, 0x000000 },  /* STAC9274X5NH */
  { 0x83847629, 0x000000 },  /* STAC9274D5NH */
  { 0x83847645, 0x000000 },  /* 92HD206X */
  { 0x83847646, 0x000000 },  /* 92HD206D */
};
static CONST HAK_HDA_PINCFG mHakPins_stac927x[] = {
  { 0x0a, 0x0221401f },
  { 0x0b, 0x02a19120 },
  { 0x0c, 0x40000100 },
  { 0x0d, 0x01014011 },
  { 0x0e, 0x01a19021 },
  { 0x0f, 0x01813024 },
  { 0x10, 0x40000100 },
  { 0x11, 0x40000100 },
  { 0x12, 0x40000100 },
  { 0x13, 0x40000100 },
  { 0x14, 0x40000100 },
  { 0x21, 0x40000100 },
  { 0x22, 0x40000100 },
  { 0x23, 0x40000100 },
  { 0x00, 0x00000000 },
  { 0x0a, 0x02211230 },
  { 0x0b, 0x02a11220 },
  { 0x0c, 0x01a19040 },
  { 0x0d, 0x01114210 },
  { 0x0e, 0x01111212 },
  { 0x0f, 0x01116211 },
  { 0x10, 0x01813050 },
  { 0x11, 0x01112214 },
  { 0x12, 0x403003fa },
  { 0x13, 0x90a60040 },
  { 0x14, 0x90a60040 },
  { 0x21, 0x404003fb },
  { 0x22, 0x40c003fc },
  { 0x23, 0x40000100 },
  { 0x00, 0x00000000 },
  { 0x0f, 0x0221101f },
  { 0x0e, 0x02a79130 },
  { 0x00, 0x00000000 },
  { 0x21, 0x01442070 },
  { 0x00, 0x00000000 },
  { 0x0a, 0x02214020 },
  { 0x0b, 0x02a19080 },
  { 0x0c, 0x0181304e },
  { 0x0d, 0x01014010 },
  { 0x0e, 0x01a19040 },
  { 0x0f, 0x01011012 },
  { 0x10, 0x01016011 },
  { 0x11, 0x40000100 },
  { 0x12, 0x40000100 },
  { 0x13, 0x40000100 },
  { 0x14, 0x40000100 },
  { 0x21, 0x01442070 },
  { 0x22, 0x40000100 },
  { 0x23, 0x40000100 },
  { 0x00, 0x00000000 },
  { 0x0a, 0x02214020 },
  { 0x0b, 0x02a19080 },
  { 0x0c, 0x0181304e },
  { 0x0d, 0x01014010 },
  { 0x0e, 0x01a19040 },
  { 0x0f, 0x01011012 },
  { 0x10, 0x01016011 },
  { 0x11, 0x01012014 },
  { 0x12, 0x103301f0 },
  { 0x13, 0x411111f0 },
  { 0x14, 0x411111f0 },
  { 0x21, 0x01442170 },
  { 0x22, 0x411111f0 },
  { 0x23, 0x411111f0 },
  { 0x00, 0x00000000 },
};
static CONST UINT32 mHakVerbs_stac927x[] = {
  0x1b03b000,
  0x0b070101,
  0x00000000,
};
static CONST HAK_HDA_FIXUP mHakFixups_stac927x[] = {
  { 3, 0, 0, HAK_HDA_FN_GPIO_SET, 0x0 },  /* 0 STAC_D965_REF stac927x_fixup_ref() */
  { 1, 1, 2, 0, 0x0 },  /* 1 STAC_D965_3ST */
  { 2, 0, 0, 0, 0x0 },  /* 2 STAC_D965_VERBS */
  { 1, 1, 4, 15, 0x0 },  /* 3 STAC_DELL_3ST */
  { 3, 0, 0, HAK_HDA_FN_IDT_DELL_DMIC_GPIO2, 0x0 },  /* 4 STAC_927X_DELL_DMIC stac927x_fixup_dell_dmic() */
  { 1, 1, 4, 30, 0x0 },  /* 5 STAC_DELL_BIOS */
  { 1, 1, 5, 33, 0x0 },  /* 6 STAC_DELL_BIOS_SPDIF */
  { 1, 1, 2, 35, 0x0 },  /* 7 STAC_D965_5ST */
  { 1, 0, 0, 50, 0x0 },  /* 8 STAC_NEMO_DEFAULT */
};
static CONST HAK_HDA_QUIRK mHakQuirks_stac927x[] = {
  { 0x8086, 0x2668, 0xffff, 0x0000 },  /* DFI LanParty -> STAC_D965_REF */
  { 0x15bd, 0x3101, 0xffff, 0x0000 },  /* DFI LanParty -> STAC_D965_REF */
  { 0x8086, 0x3d01, 0xffff, 0x0001 },  /* Intel D946 -> STAC_D965_3ST */
  { 0x8086, 0xa301, 0xffff, 0x0001 },  /* Intel D946 -> STAC_D965_3ST */
  { 0x8086, 0x2100, 0xff00, 0x0001 },  /* Intel D965 -> STAC_D965_3ST */
  { 0x8086, 0x2000, 0xff00, 0x0001 },  /* Intel D965 -> STAC_D965_3ST */
  { 0x1028, 0x01dd, 0xffff, 0x0003 },  /* Dell Dimension E520 -> STAC_DELL_3ST */
  { 0x1028, 0x01ed, 0xffff, 0x0003 },  /* Dell      -> STAC_DELL_3ST */
  { 0x1028, 0x01f4, 0xffff, 0x0003 },  /* Dell      -> STAC_DELL_3ST */
  { 0x1028, 0x01f3, 0xffff, 0x0005 },  /* Dell Inspiron 1420 -> STAC_DELL_BIOS */
  { 0x1028, 0x01f7, 0xffff, 0x0005 },  /* Dell XPS M1730 -> STAC_DELL_BIOS */
  { 0x1028, 0x0227, 0xffff, 0x0005 },  /* Dell Vostro 1400   -> STAC_DELL_BIOS */
  { 0x1028, 0x022e, 0xffff, 0x0006 },  /* Dell      -> STAC_DELL_BIOS_SPDIF */
  { 0x1028, 0x022f, 0xffff, 0x0005 },  /* Dell Inspiron 1525 -> STAC_DELL_BIOS */
  { 0x1028, 0x0242, 0xffff, 0x0005 },  /* Dell      -> STAC_DELL_BIOS */
  { 0x1028, 0x0243, 0xffff, 0x0005 },  /* Dell      -> STAC_DELL_BIOS */
  { 0x1028, 0x02ff, 0xffff, 0x0005 },  /* Dell      -> STAC_DELL_BIOS */
  { 0x1028, 0x0209, 0xffff, 0x0006 },  /* Dell XPS 1330 -> STAC_DELL_BIOS_SPDIF */
  { 0x8086, 0x2300, 0xff00, 0x0007 },  /* Intel D965 -> STAC_D965_5ST */
  { 0x8086, 0x2500, 0xff00, 0x0007 },  /* Intel D965 -> STAC_D965_5ST */
  { 0x1888, 0x1000, 0xffff, 0x0008 },  /* AmigaOne X1000 -> STAC_NEMO_DEFAULT */
  { 0x10cf, 0x0000, 0x0000, 0x7fff },  /* FSC -> STAC_927X_VOLKNOB (nothing to do) */
};

/* stac9205: sound/hda/codecs/sigmatel.c  (stac9205_fixups + stac9205_fixup_tbl) */
static CONST HAK_HDA_CODEC_ID mHakIds_stac9205[] = {
  { 0x83847698, 0x000000 },  /* STAC9205 */
  { 0x838476a0, 0x000000 },  /* STAC9205 */
  { 0x838476a1, 0x000000 },  /* STAC9205D */
  { 0x838476a2, 0x000000 },  /* STAC9204 */
  { 0x838476a3, 0x000000 },  /* STAC9204D */
  { 0x838476a4, 0x000000 },  /* STAC9255 */
  { 0x838476a5, 0x000000 },  /* STAC9255D */
  { 0x838476a6, 0x000000 },  /* STAC9254 */
  { 0x838476a7, 0x000000 },  /* STAC9254D */
};
static CONST HAK_HDA_PINCFG mHakPins_stac9205[] = {
  { 0x0a, 0x0321101f },
  { 0x0b, 0x03a11020 },
  { 0x0c, 0x400003fa },
  { 0x0d, 0x90170310 },
  { 0x0e, 0x400003fb },
  { 0x0f, 0x400003fc },
  { 0x14, 0x400003fd },
  { 0x16, 0x40f000f9 },
  { 0x17, 0x90a60330 },
  { 0x18, 0x400003ff },
  { 0x21, 0x0144131f },
  { 0x22, 0x40c003fe },
  { 0x00, 0x00000000 },
  { 0x0a, 0x0421101f },
  { 0x0b, 0x04a11020 },
  { 0x0c, 0x400003fa },
  { 0x0d, 0x90170310 },
  { 0x0e, 0x400003fb },
  { 0x0f, 0x400003fc },
  { 0x14, 0x400003fd },
  { 0x16, 0x400003f9 },
  { 0x17, 0x90a60330 },
  { 0x18, 0x400003ff },
  { 0x21, 0x01441340 },
  { 0x22, 0x40c003fe },
  { 0x00, 0x00000000 },
};
static CONST HAK_HDA_FIXUP mHakFixups_stac9205[] = {
  { 3, 0, 0, HAK_HDA_FN_GPIO_SET, 0x0 },  /* 0 STAC_9205_REF stac9205_fixup_ref() */
  { 1, 0, 0, 0, 0x0 },  /* 1 STAC_9205_DELL_M42 */
  { 3, 0, 0, HAK_HDA_FN_GPIO_SET, 0x1b0b01 },  /* 2 STAC_9205_DELL_M43 stac9205_fixup_dell_m43() */
  { 1, 0, 0, 13, 0x0 },  /* 3 STAC_9205_DELL_M44 */
};
static CONST HAK_HDA_QUIRK mHakQuirks_stac9205[] = {
  { 0x8086, 0x2668, 0xffff, 0x0000 },  /* DFI LanParty -> STAC_9205_REF */
  { 0x8086, 0xfb30, 0xffff, 0x0000 },  /* SigmaTel -> STAC_9205_REF */
  { 0x15bd, 0x3101, 0xffff, 0x0000 },  /* DFI LanParty -> STAC_9205_REF */
  { 0x1028, 0x01f1, 0xffff, 0x0001 },  /* unknown Dell -> STAC_9205_DELL_M42 */
  { 0x1028, 0x01f2, 0xffff, 0x0001 },  /* unknown Dell -> STAC_9205_DELL_M42 */
  { 0x1028, 0x01f8, 0xffff, 0x0002 },  /* Dell Precision -> STAC_9205_DELL_M43 */
  { 0x1028, 0x01f9, 0xffff, 0x0002 },  /* Dell Precision -> STAC_9205_DELL_M43 */
  { 0x1028, 0x01fa, 0xffff, 0x0002 },  /* Dell Precision -> STAC_9205_DELL_M43 */
  { 0x1028, 0x01fc, 0xffff, 0x0001 },  /* unknown Dell -> STAC_9205_DELL_M42 */
  { 0x1028, 0x01fd, 0xffff, 0x0001 },  /* unknown Dell -> STAC_9205_DELL_M42 */
  { 0x1028, 0x01fe, 0xffff, 0x0002 },  /* Dell Precision -> STAC_9205_DELL_M43 */
  { 0x1028, 0x01ff, 0xffff, 0x0002 },  /* Dell Precision M4300 -> STAC_9205_DELL_M43 */
  { 0x1028, 0x0204, 0xffff, 0x0001 },  /* unknown Dell -> STAC_9205_DELL_M42 */
  { 0x1028, 0x0206, 0xffff, 0x0002 },  /* Dell Precision -> STAC_9205_DELL_M43 */
  { 0x1028, 0x021b, 0xffff, 0x0002 },  /* Dell Precision -> STAC_9205_DELL_M43 */
  { 0x1028, 0x021c, 0xffff, 0x0002 },  /* Dell Precision -> STAC_9205_DELL_M43 */
  { 0x1028, 0x021f, 0xffff, 0x0003 },  /* Dell Inspiron -> STAC_9205_DELL_M44 */
  { 0x1028, 0x0228, 0xffff, 0x0001 },  /* Dell Vostro 1500 -> STAC_9205_DELL_M42 */
  { 0x1028, 0x0229, 0xffff, 0x0001 },  /* Dell Vostro 1700 -> STAC_9205_DELL_M42 */
  { 0x107b, 0x0560, 0xffff, 0x7fff },  /* Gateway T6834c -> STAC_9205_EAPD (nothing to do) */
  { 0x107b, 0x0565, 0xffff, 0x7fff },  /* Gateway T1616 -> STAC_9205_EAPD (nothing to do) */
};

/* stac92hd95: sound/hda/codecs/sigmatel.c  (stac92hd95_fixups + stac92hd95_fixup_tbl) */
static CONST HAK_HDA_CODEC_ID mHakIds_stac92hd95[] = {
  { 0x111d7695, 0x000000 },  /* 92HD95 */
};
static CONST UINT32 mHakVerbs_stac92hd95[] = {
  0x1a079500,
  0x00000000,
};
static CONST HAK_HDA_FIXUP mHakFixups_stac92hd95[] = {
  { 2, 1, 1, 0, 0x0 },  /* 0 STAC_92HD95_HP_BASS */
  { 0, 0, 0, 0, 0x0 },  /* 1 STAC_92HD95_HP_LED stac92hd95_fixup_hp_led() NOT PORTED */
};
static CONST HAK_HDA_QUIRK mHakQuirks_stac92hd95[] = {
  { 0x103c, 0x1911, 0xffff, 0x0000 },  /* HP Spectre 13 -> STAC_92HD95_HP_BASS */
};

/* stac9872: sound/hda/codecs/sigmatel.c  (stac9872_fixups + stac9872_fixup_tbl) */
static CONST HAK_HDA_CODEC_ID mHakIds_stac9872[] = {
  { 0x83847661, 0x000000 },  /* CXD9872RD/K */
  { 0x83847662, 0x000000 },  /* STAC9872AK */
  { 0x83847664, 0x000000 },  /* CXD9872AKD */
};
static CONST HAK_HDA_PINCFG mHakPins_stac9872[] = {
  { 0x0a, 0x03211020 },
  { 0x0b, 0x411111f0 },
  { 0x0c, 0x411111f0 },
  { 0x0d, 0x03a15030 },
  { 0x0e, 0x411111f0 },
  { 0x0f, 0x90170110 },
  { 0x11, 0x411111f0 },
  { 0x13, 0x411111f0 },
  { 0x14, 0x90a7013e },
  { 0x00, 0x00000000 },
};
static CONST HAK_HDA_FIXUP mHakFixups_stac9872[] = {
  { 1, 0, 0, 0, 0x0 },  /* 0 STAC_9872_VAIO */
};
static CONST HAK_HDA_QUIRK mHakQuirks_stac9872[] = {
  { 0x104d, 0x81e0, 0xfff0, 0x0000 },  /* Sony VAIO F/S -> STAC_9872_VAIO */
};

/* vt2002p: sound/hda/codecs/via.c  (via_fixups + vt2002p_fixups) */
static CONST HAK_HDA_CODEC_ID mHakIds_vt2002p[] = {
  { 0x11060438, 0x000000 },  /* VT2002P */
  { 0x11064438, 0x000000 },  /* VT2002P */
  { 0x11060446, 0x000000 },  /* VT1802 */
  { 0x11068446, 0x000000 },  /* VT1802 */
};
static CONST HAK_HDA_PINCFG mHakPins_vt2002p[] = {
  { 0x24, 0x991301f0 },
  { 0x33, 0x991301f1 },
  { 0x00, 0x00000000 },
};
static CONST HAK_HDA_FIXUP mHakFixups_vt2002p[] = {
  { 1, 0, 0, 0, 0x0 },  /* 0 VIA_FIXUP_ASUS_G75 */
};
static CONST HAK_HDA_QUIRK mHakQuirks_vt2002p[] = {
  { 0x1043, 0x13f7, 0xffff, 0x7fff },  /* Asus B23E -> VIA_FIXUP_POWER_SAVE (nothing to do) */
  { 0x1043, 0x1487, 0xffff, 0x0000 },  /* Asus G75 -> VIA_FIXUP_ASUS_G75 */
  { 0x1043, 0x8532, 0xffff, 0x7fff },  /* Asus X202E -> VIA_FIXUP_INTMIC_BOOST (nothing to do) */
  { 0x1558, 0x0000, 0x0000, 0x7fff },  /* Clevo -> VIA_FIXUP_POWER_SAVE (nothing to do) */
};

/* ad1986a: sound/hda/codecs/analog.c  (ad1986a_fixups + ad1986a_fixup_tbl) */
static CONST HAK_HDA_CODEC_ID mHakIds_ad1986a[] = {
  { 0x11d41986, 0x000000 },  /* AD1986A */
};
static CONST HAK_HDA_PINCFG mHakPins_ad1986a[] = {
  { 0x1d, 0x90a7013e },
  { 0x00, 0x00000000 },
  { 0x1a, 0x02214021 },
  { 0x1b, 0x01014011 },
  { 0x1c, 0x01813030 },
  { 0x1d, 0x01a19020 },
  { 0x1e, 0x411111f0 },
  { 0x1f, 0x02a190f0 },
  { 0x20, 0x411111f0 },
  { 0x00, 0x00000000 },
  { 0x1a, 0x02214021 },
  { 0x1b, 0x90170110 },
  { 0x1c, 0x411111f0 },
  { 0x1d, 0x411111f0 },
  { 0x1e, 0x411111f0 },
  { 0x1f, 0x02a191f0 },
  { 0x20, 0x411111f0 },
  { 0x00, 0x00000000 },
  { 0x1b, 0x90170110 },
  { 0x1d, 0x90a7013e },
  { 0x20, 0x411111f0 },
  { 0x24, 0x411111f0 },
  { 0x00, 0x00000000 },
  { 0x1b, 0x90170110 },
  { 0x1d, 0x90a7013e },
  { 0x00, 0x00000000 },
  { 0x1a, 0x0421402f },
  { 0x1b, 0x90170110 },
  { 0x1c, 0x411111f0 },
  { 0x1d, 0x90a70130 },
  { 0x1e, 0x411111f0 },
  { 0x1f, 0x04a19040 },
  { 0x20, 0x411111f0 },
  { 0x21, 0x411111f0 },
  { 0x22, 0x411111f0 },
  { 0x23, 0x411111f0 },
  { 0x24, 0x411111f0 },
  { 0x25, 0x411111f0 },
  { 0x00, 0x00000000 },
};
static CONST HAK_HDA_FIXUP mHakFixups_ad1986a[] = {
  { 1, 0, 3, 0, 0x0 },  /* 0 AD1986A_FIXUP_LAPTOP_IMIC */
  { 3, 0, 0, HAK_HDA_FN_EAPD_NOT_INVERTED, 0x0 },  /* 1 AD1986A_FIXUP_EAPD ad1986a_fixup_eapd() */
  { 1, 0, 0, 2, 0x0 },  /* 2 AD1986A_FIXUP_3STACK */
  { 1, 0, 0, 10, 0x0 },  /* 3 AD1986A_FIXUP_LAPTOP */
  { 1, 0, 0, 18, 0x0 },  /* 4 AD1986A_FIXUP_SAMSUNG */
  { 1, 0, 0, 23, 0x0 },  /* 5 AD1986A_FIXUP_ULTRA */
  { 1, 1, 7, 26, 0x0 },  /* 6 AD1986A_FIXUP_EASYNOTE */
  { 0, 0, 0, 0, 0x0 },  /* 7 AD1986A_FIXUP_EAPD_MIX_IN ad1986a_fixup_eapd_mix_in() NOT PORTED */
};
static CONST HAK_HDA_QUIRK mHakQuirks_ad1986a[] = {
  { 0x103c, 0x30af, 0xffff, 0x0000 },  /* HP B2800 -> AD1986A_FIXUP_LAPTOP_IMIC */
  { 0x1043, 0x1153, 0xffff, 0x0000 },  /* ASUS M9V -> AD1986A_FIXUP_LAPTOP_IMIC */
  { 0x1043, 0x1443, 0xffff, 0x0001 },  /* ASUS Z99He -> AD1986A_FIXUP_EAPD */
  { 0x1043, 0x1447, 0xffff, 0x0001 },  /* ASUS A8JN -> AD1986A_FIXUP_EAPD */
  { 0x1043, 0x8100, 0xff00, 0x0002 },  /* ASUS P5 -> AD1986A_FIXUP_3STACK */
  { 0x1043, 0x8200, 0xff00, 0x0002 },  /* ASUS M2 -> AD1986A_FIXUP_3STACK */
  { 0x10de, 0xcb84, 0xffff, 0x0002 },  /* ASUS A8N-VM -> AD1986A_FIXUP_3STACK */
  { 0x1179, 0xff40, 0xffff, 0x0001 },  /* Toshiba Satellite L40 -> AD1986A_FIXUP_EAPD */
  { 0x144d, 0xc01e, 0xffff, 0x0003 },  /* FSC V2060 -> AD1986A_FIXUP_LAPTOP */
  { 0x144d, 0xc000, 0xff00, 0x0004 },  /* Samsung -> AD1986A_FIXUP_SAMSUNG */
  { 0x144d, 0xc027, 0xffff, 0x0005 },  /* Samsung Q1 -> AD1986A_FIXUP_ULTRA */
  { 0x1631, 0xc022, 0xffff, 0x0006 },  /* PackardBell EasyNote MX65 -> AD1986A_FIXUP_EASYNOTE */
  { 0x17aa, 0x2066, 0xffff, 0x7fff },  /* Lenovo N100 -> AD1986A_FIXUP_INV_JACK_DETECT (nothing to do) */
  { 0x17aa, 0x1011, 0xffff, 0x0002 },  /* Lenovo M55 -> AD1986A_FIXUP_3STACK */
  { 0x17aa, 0x1017, 0xffff, 0x0002 },  /* Lenovo A60 -> AD1986A_FIXUP_3STACK */
};

/* ad1981: sound/hda/codecs/analog.c  (ad1981_fixups + ad1981_fixup_tbl) */
static CONST HAK_HDA_CODEC_ID mHakIds_ad1981[] = {
  { 0x11d41981, 0x000000 },  /* AD1981 */
};
static CONST HAK_HDA_QUIRK mHakQuirks_ad1981[] = {
  { 0x1014, 0x0000, 0x0000, 0x7fff },  /* Lenovo -> AD1981_FIXUP_AMP_OVERRIDE (nothing to do) */
  { 0x103c, 0x0000, 0x0000, 0x7fff },  /* HP -> AD1981_FIXUP_HP_EAPD (nothing to do) */
  { 0x17aa, 0x0000, 0x0000, 0x7fff },  /* Lenovo -> AD1981_FIXUP_AMP_OVERRIDE (nothing to do) */
  { 0x30b0, 0x103c, 0xffff, 0x7fff },  /* HP nx6320 -> AD1981_FIXUP_HP_EAPD (nothing to do) */
};

/* ad1884: sound/hda/codecs/analog.c  (ad1884_fixups + ad1884_fixup_tbl) */
static CONST HAK_HDA_CODEC_ID mHakIds_ad1884[] = {
  { 0x11d4184a, 0x000000 },  /* AD1884A */
  { 0x11d41883, 0x000000 },  /* AD1883 */
  { 0x11d41884, 0x000000 },  /* AD1884 */
  { 0x11d4194a, 0x000000 },  /* AD1984A */
  { 0x11d4194b, 0x000000 },  /* AD1984B */
  { 0x11d41984, 0x000000 },  /* AD1984 */
};
static CONST UINT32 mHakVerbs_ad1884[] = {
  0x010513f7,
  0x01040008,
  0x00000000,
  0x010513f7,
  0x01040008,
  0x00000000,
};
static CONST HAK_HDA_FIXUP mHakFixups_ad1884[] = {
  { 2, 1, 1, 0, 0x0 },  /* 0 AD1884_FIXUP_HP_TOUCHSMART */
  { 3, 1, 2, HAK_HDA_FN_GPIO_SET, 0x20200 },  /* 1 AD1884_FIXUP_HP_EAPD ad1884_fixup_hp_eapd() */
  { 0, 0, 0, 0, 0x0 },  /* 2 AD1884_FIXUP_AMP_OVERRIDE ad1884_fixup_amp_override() NOT PORTED */
  { 0, 1, 4, 0, 0x0 },  /* 3 AD1884_FIXUP_THINKPAD ad1884_fixup_thinkpad() NOT PORTED */
  { 2, 0, 0, 3, 0x0 },  /* 4 AD1884_FIXUP_DMIC_COEF */
};
static CONST HAK_HDA_QUIRK mHakQuirks_ad1884[] = {
  { 0x103c, 0x2a82, 0xffff, 0x0000 },  /* HP Touchsmart -> AD1884_FIXUP_HP_TOUCHSMART */
  { 0x103c, 0x0000, 0x0000, 0x0001 },  /* HP -> AD1884_FIXUP_HP_EAPD */
  { 0x17aa, 0x0000, 0x0000, 0x0003 },  /* Lenovo Thinkpad -> AD1884_FIXUP_THINKPAD */
};

static CONST HAK_HDA_FAMILY mHakFamilies[] = {
  { "alc269", mHakIds_alc269, 42, mHakFixups_alc269, 254, mHakQuirks_alc269, 1236, mHakPins_alc269, mHakVerbs_alc269, 0 },
  { "alc882", mHakIds_alc882, 15, mHakFixups_alc882, 36, mHakQuirks_alc882, 110, mHakPins_alc882, mHakVerbs_alc882, 0 },
  { "alc662", mHakIds_alc662, 12, mHakFixups_alc662, 42, mHakQuirks_alc662, 69, mHakPins_alc662, mHakVerbs_alc662, 0 },
  { "alc880", mHakIds_alc880, 1, mHakFixups_alc880, 25, mHakQuirks_alc880, 59, mHakPins_alc880, mHakVerbs_alc880, 0 },
  { "alc260", mHakIds_alc260, 1, mHakFixups_alc260, 8, mHakQuirks_alc260, 12, mHakPins_alc260, mHakVerbs_alc260, 0 },
  { "alc262", mHakIds_alc262, 1, mHakFixups_alc262, 7, mHakQuirks_alc262, 10, mHakPins_alc262, mHakVerbs_alc262, 0 },
  { "alc268", mHakIds_alc268, 2, mHakFixups_alc268, 2, mHakQuirks_alc268, 3, mHakPins_alc268, mHakVerbs_alc268, 0 },
  { "alc861", mHakIds_alc861, 2, mHakFixups_alc861, 2, mHakQuirks_alc861, 7, mHakPins_alc861, mHakVerbs_alc861, 0 },
  { "alc861vd", mHakIds_alc861vd, 2, NULL, 0, mHakQuirks_alc861vd, 3, NULL, NULL, 0 },
  { "cxt5045", mHakIds_cxt5045, 1, mHakFixups_cxt5045, 3, mHakQuirks_cxt5045, 6, mHakPins_cxt5045, NULL, 0 },
  { "cxt5047", mHakIds_cxt5047, 1, NULL, 0, mHakQuirks_cxt5047, 1, NULL, NULL, 0 },
  { "cxt5051", mHakIds_cxt5051, 1, mHakFixups_cxt5051, 2, mHakQuirks_cxt5051, 2, mHakPins_cxt5051, NULL, 0 },
  { "cxt5066", mHakIds_cxt5066, 30, mHakFixups_cxt5066, 19, mHakQuirks_cxt5066, 58, mHakPins_cxt5066, mHakVerbs_cxt5066, 0 },
  { "sn6186", mHakIds_sn6186, 1, NULL, 0, NULL, 0, NULL, NULL, 0 },
  { "cs420x", mHakIds_cs420x, 2, mHakFixups_cs420x, 7, mHakQuirks_cs420x, 12, mHakPins_cs420x, mHakVerbs_cs420x, 0 },
  { "cs4208", mHakIds_cs4208, 1, mHakFixups_cs4208, 1, mHakQuirks_cs4208, 2, NULL, NULL, 0 },
  { "cs421x", mHakIds_cs421x, 1, mHakFixups_cs421x, 2, mHakQuirks_cs421x, 1, mHakPins_cs421x, NULL, 0 },
  { "stac9200", mHakIds_stac9200, 1, mHakFixups_stac9200, 16, mHakQuirks_stac9200, 37, mHakPins_stac9200, mHakVerbs_stac9200, 0 },
  { "stac925x", mHakIds_stac925x, 6, mHakFixups_stac925x, 8, mHakQuirks_stac925x, 13, mHakPins_stac925x, NULL, 0 },
  { "stac92hd73xx", mHakIds_stac92hd73xx, 18, mHakFixups_stac92hd73xx, 5, mHakQuirks_stac92hd73xx, 26, mHakPins_stac92hd73xx, NULL, HAK_HDA_FAM_IDT_GPIO0_EAPD },
  { "stac92hd83xxx", mHakIds_stac92hd83xxx, 27, mHakFixups_stac92hd83xxx, 12, mHakQuirks_stac92hd83xxx, 94, mHakPins_stac92hd83xxx, mHakVerbs_stac92hd83xxx, 0 },
  { "stac92hd71bxx", mHakIds_stac92hd71bxx, 10, mHakFixups_stac92hd71bxx, 4, mHakQuirks_stac92hd71bxx, 26, mHakPins_stac92hd71bxx, NULL, HAK_HDA_FAM_IDT_GPIO0_EAPD },
  { "stac922x", mHakIds_stac922x, 6, mHakFixups_stac922x, 8, mHakQuirks_stac922x, 39, mHakPins_stac922x, NULL, 0 },
  { "stac927x", mHakIds_stac927x, 19, mHakFixups_stac927x, 9, mHakQuirks_stac927x, 22, mHakPins_stac927x, mHakVerbs_stac927x, HAK_HDA_FAM_IDT_GPIO0_EAPD },
  { "stac9205", mHakIds_stac9205, 9, mHakFixups_stac9205, 4, mHakQuirks_stac9205, 21, mHakPins_stac9205, NULL, HAK_HDA_FAM_IDT_GPIO0_EAPD },
  { "stac92hd95", mHakIds_stac92hd95, 1, mHakFixups_stac92hd95, 2, mHakQuirks_stac92hd95, 1, NULL, mHakVerbs_stac92hd95, 0 },
  { "stac9872", mHakIds_stac9872, 3, mHakFixups_stac9872, 1, mHakQuirks_stac9872, 1, mHakPins_stac9872, NULL, 0 },
  { "vt2002p", mHakIds_vt2002p, 4, mHakFixups_vt2002p, 1, mHakQuirks_vt2002p, 4, mHakPins_vt2002p, NULL, 0 },
  { "ad1986a", mHakIds_ad1986a, 1, mHakFixups_ad1986a, 8, mHakQuirks_ad1986a, 15, mHakPins_ad1986a, NULL, HAK_HDA_FAM_EAPD_INVERTED },
  { "ad1981", mHakIds_ad1981, 1, NULL, 0, mHakQuirks_ad1981, 4, NULL, NULL, 0 },
  { "ad1884", mHakIds_ad1884, 6, mHakFixups_ad1884, 5, mHakQuirks_ad1884, 3, NULL, mHakVerbs_ad1884, 0 },
};

#endif
