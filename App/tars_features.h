#ifndef TARS_FEATURES_H
#define TARS_FEATURES_H

/*
 * Board feature switches.
 *
 * The STM32F429I-DISC1 is being used as the motor-control variant: TIM1's
 * 6-channel complementary PWM needs pins that the on-board LCD owns
 * (PA6/PA8 + PB0/PB1 via LTDC/I2C3), so the LCD stack (LTDC + DMA2D + SPI5 +
 * I2C3) is compiled OUT by default and those pins are freed for the inverter.
 *
 * The LCD source is NOT deleted — it is gated behind TARS_FEATURE_LCD so a
 * future display build can re-enable it with:
 *     cmake -DTARS_ENABLE_LCD=ON ...
 * (the root CMakeLists turns that option into -DTARS_FEATURE_LCD=1). When the
 * macro is unset, the default below applies.
 */

#ifndef TARS_FEATURE_LCD
#define TARS_FEATURE_LCD 0
#endif

#endif /* TARS_FEATURES_H */
