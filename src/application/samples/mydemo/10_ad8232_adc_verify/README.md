# AD8232 ADC TJC Verify

This sample is a minimal verification path for AD8232 ADC sampling and TJC waveform display.

It reuses the pre-SLE AD8232 ADC sample sources:

- `../0_ad8232_adc/main.c`
- `../0_ad8232_adc/ad8232_bsp.c`
- `../0_ad8232_adc/ecg_processor.c`
- `../0_ad8232_adc/tjc_display.c`

Enable `CONFIG_SAMPLE_SUPPORT_AD8232_ADC_VERIFY=y` to build only this verification sample before testing SLE display integration.
