# modjpeg-nginx

nginx filter module for [libmodjpeg](https://github.com/ioppermann/libmodjpeg)


## Typical Uses

- You are a photographer and have a gallery of JPEG images on your website.
- Online shop with product images
- Paywall
- Personalized logo on image
- Avatar on image
- Visual watermark


## Installation

```
# ./configure --add_module="/path/to/modjpeg-nginx"
```

## Synopsis

```nginx
   ...

   location /gallery {
   	# enable jpeg filter module
   	jpeg_filter on;

   	# limit image sizes to 9 megapixel pixel
   	jpeg_filter_max_pixel 9000000;

   	# limit image file size to 5 megabytes
   	jpeg_filter_buffer 5M;

   	# deliver the images unmodified if one of the limits apply
   	jpeg_filter_graceful on;

   	# pixelate the image
   	jpeg_filter_effect pixelate;

   	# add a masked logo in the bottom right corner
   	# with a distance of 10 pixel from the border
   	jpeg_filter_dropon_align bottom right;
   	jpeg_filter_dropon_offset -10 -10;
   	jpeg_filter_dropon /path/to/logo.jpg /path/to/mask.jpg
   }

   ...
```

## Directives

- [jpeg_filter](#jpeg_filter)
- [jpeg_filter_max_pixel](#jpeg_filter_max_pixel)
- [jpeg_filter_buffer](#jpeg_filter_buffer)
- [jpeg_filter_optimize](#jpeg_filter_optimize)
- [jpeg_filter_progressive](#jpeg_filter_progressive)
- [jpeg_filter_arithmetric](#jpeg_filter_arithmetric)
- [jpeg_filter_graceful](#jpeg_filter_graceful)
- [jpeg_filter_effect](#jpeg_filter_effect)
- [jpeg_filter_dropon_align](#jpeg_filter_dropon_align)
- [jpeg_filter_dropon_offset](#jpeg_filter_dropon_offset)
- [jpeg_filter_dropon](#jpeg_filter_dropon)
- [Notes](#notes)


### jpeg_filter

__Syntax:__ `jpeg_filter on | off`

__Default:__ `jpeg_filter off`

__Context:__ `location`

Enable the jpeg filter module.

This directive is turned off by default.


### jpeg_filter_max_pixel

__Syntax:__ `jpeg_filter_max_pixel pixel`

__Default:__ `0`

__Context:__ `http, server, location`

Maximum number of pixel in image to operate on. If the image has more pixel (width * height) than `pixel`, the jpeg filter will return a  "415 Unsupported Media Type".
Set [jpeg_filter_graceful](#jpeg_filter_graceful) to `on` to deliver the image unchanged. Set the maximum pixel to 0 in order ignore the image dimensions.

This directive is set to 0 by default.


### jpeg_filter_buffer

__Syntax:__ `jpeg_filter_buffer size`

__Default:__ `2M`

__Context:__ `http, server, location`

The maximum file size of the image to operate on. If the file size if bigger than `size`, the jpeg filter will return a "415 Unsupported Media Type".
Set [jpeg_filter_graceful](#jpeg_filter_graceful) to `on` to deliver the image unchanged.

This directive is set to 2 megabyte by default.


### jpeg_filter_optimize

__Syntax:__ `jpeg_filter_optimize on | off`

__Default:__ `off`

__Context:__ `http, server, location`

Upon delivery, optimize the Huffman tables of the image.

This directive is turned off by default.


### jpeg_filter_progressive

__Syntax:__ `jpeg_filter_progressive on | off`

__Default:__ `off`

__Context:__ `http, server, location`

Upon delivery, enable progressive encoding of the image.

This directive is turned off by default.


### jpeg_filter_arithmetric

__Syntax:__ `jpeg_filter_arithmetric on | off`

__Default:__ `off`

__Context:__ `http, server, location`

Upon delivery, enable arithmetric encoding of the image.
This will override the [jpeg_filter_optimize](#jpeg_filter_optimize) directive.
Arithmetric encoding is usually not supported by browsers.

This directive is turned off by default.


### jpeg_filter_graceful

__Syntax:__ `jpeg_filter_graceful on | off`

__Default:__ `off`

__Context:__ `http, server, location`

Allow to deliver the unchanged image in case the directives [jpeg_filter_max_width](#jpeg_filter_max_width), [jpeg_filter_max_height](#jpeg_filter_max_height), or [jpeg_filter_buffer](#jpeg_filter_buffer) would return a "415 Unsupported Media Type" error.

This directive is turned off by default.


### jpeg_filter_effect

__Syntax:__ `jpeg_filter_effect grayscale | pixelate`

__Syntax:__ `jpeg_filter_effect darken | brighten value`

__Syntax:__ `jpeg_filter_effect tintblue | tintyellow | tintred | tintgreen value`

__Default:__ `-`

__Context:__ `location`

Apply an effect to the image.

`grayscale` will remove all color components from the image. This only applies to images in the YCbCr color space.

`pixelate` will pixelate the image in blocks of 8x8 pixel by setting the AC coefficients in all components to 0.

`darken` will darken the image by decreasing the DC coefficients in the Y component by `value`. This only applies to images in the YCbCr color space.

`brighten` will brighten the image by increasing the DC coefficients in the Y component by `value`. This only applies to images in the YCbCr color space.

`tintblue` will tint the image blue by increasing the DC coefficients in the Cb component by `value`. This only applies to images in the YCbCr color space.

`tintyellow` will tint the image blue by decreasing the DC coefficients in the Cb component by `value`. This only applies to images in the YCbCr color space.

`tintred` will tint the image red by increasing the DC coefficients in the Cr component by `value`. This only applies to images in the YCbCr color space.

`tintgreen` will tint the image green by decreasing the DC coefficients in the Cr component by `value`. This only applies to images in the YCbCr color space.

This directive is not set by default.

Both parameter can contain variables.


### jpeg_filter_dropon_align

__Syntax:__ `jpeg_filter_dropon_align [top | center | bottom] [left | center | right]`

__Default:__ `center center`

__Context:__ `location`

Align the dropon on the image. Use the directive [jpeg_filter_dropon_offset](#jpeg_filter_dropon_offset) to offset the dropon from the alignment.

This directive must be set before [jpeg_filter_dropon](#jpeg_filter_dropon) in order to have an effect on the dropon.

This directive will apply the dropon in the center of the image by default.

All parameters can contain variables.


### jpeg_filter_dropon_offset

__Syntax:__ `jpeg_filter_dropon_offset vertical horizontal`

__Default:__ `0 0`

__Context:__ `location`

Offset the dropon by `vertical` and `horizontal` pixels from the alignment given with the [jpeg_filter_dropon_align](#jpeg_filter_dropon_align) directive.
Use a negative value to move the dropon up or left and a positive value to move the dropon down or right.

This directive must be set before [jpeg_filter_dropon](#jpeg_filter_dropon) in order to have an effect on the dropon.

This directive will not apply an offset by default.

All parameters can contain variables.


### jpeg_filter_dropon

__Syntax:__ `jpeg_filter_dropon image`

__Syntax:__ `jpeg_filter_dropon image mask`

__Default:__ `-`

__Context:__ `location`

Apply a dropon to the image. The dropon is given by a path to a JPEG image for `image` and optionally a path to a JPEG image for `mask`. If no mask image is
provided, the image will be applied without transcluency. If a mask image is provided, only the luminance component will be used. For the mask, black means
fully transcluent and white means fully opaque. Any values inbetween will blend the underlying image and the dropon accordingly.

This directive is not set by default.

All parameters can contain variables.

If none of the parameters contains variables, the dropon is loaded during loading the configuration. If at least one parameter contains variables the dropon
will be loaded during processing the request. After processing the request the dropon will be unloaded.


### Notes

The directives `jpeg_filter_effect`, `jpeg_filter_dropon_align`, `jpeg_filter_dropon_offset`, and `jpeg_filter_dropon` are applied in the order they
appear in the nginx config file, i.e. it makes a difference if you apply first an effect and then add a dropon or vice versa. In the former case the dropon will be
unaffected by the effect and in the latter case the effect will be also applied on the dropon.


## License

This module is distributed under the BSD license. Refer to [LICENSE](/blob/master/LICENSE).


## Acknowledgement

This module is heavily inspired by the image filter module with
insights from
["Emiller’s Guide To Nginx Module Development"](https://www.evanmiller.org/nginx-modules-guide.html)
and the
[nginx development guide](https://nginx.org/en/docs/dev/development_guide.html).
