# modjpeg-nginx

nginx module for [libmodjpeg](https://github.com/ioppermann/libmodjpeg)


## Installation

```
# ./configure --add_module="..."
```

## Directives


- [jpeg_filter](#jpeg_filter)
- [jpeg_filter_max_width](#jpeg_filter_max_width)
- [jpeg_filter_max_height](#jpeg_filter_max_height)
- [jpeg_filter_optimize](#jpeg_filter_optimize)
- [jpeg_filter_progressive](#jpeg_filter_progressive)
- [jpeg_filter_arithmetric](#jpeg_filter_arithmetric)
- [jpeg_filter_graceful](#jpeg_filter_graceful)
- [jpeg_filter_buffer](#jpeg_filter_buffer)
- [jpeg_filter_effect](#jpeg_filter_effect)
- [jpeg_filter_dropon_align](#jpeg_filter_dropon_align)
- [jpeg_filter_dropon_offset](#jpeg_filter_dropon_offset)
- [jpeg_filter_dropon](#jpeg_filter_dropon)

### jpeg_filter

__Syntax:__ `jpeg_filter on | off`

__Default:__ `jpeg_filter off`

__Context:__ `location`

### jpeg_filter_max_width

__Syntax:__ `jpeg_filter_max_width width`

__Default:__ `0` (not limited)

__Context:__ `http, server, location`

### jpeg_filter_max_height

__Syntax:__ `jpeg_filter_max_height height`

__Default:__ `0` (not limited)

__Context:__ `http, server, location`

### jpeg_filter_optimize

__Syntax:__ `jpeg_filter_optimize on | off`

__Default:__ `off`

__Context:__ `http, server, location`

### jpeg_filter_progressive

__Syntax:__ `jpeg_filter_progressive on | off`

__Default:__ `off`

__Context:__ `http, server, location`

### jpeg_filter_arithmetric

__Syntax:__ `jpeg_filter_arithmetric on | off`

__Default:__ `off`

__Context:__ `http, server, location`

### jpeg_filter_graceful

__Syntax:__ `jpeg_filter_graceful on | off`

__Default:__ `off`

__Context:__ `http, server, location`

### jpeg_filter_buffer

__Syntax:__ `jpeg_filter_buffer size`

__Default:__ `2M`

__Context:__ `http, server, location`

### jpeg_filter_effect

__Syntax:__ `jpeg_filter_effect grayscale | pixelate`

__Syntax:__ `jpeg_filter_effect darken | brighten value`

__Syntax:__ `jpeg_filter_effect tintblue | tintyellow | tintred | tintgreen value`

__Default:__ `-`

__Context:__ `location`

### jpeg_filter_dropon_align

__Syntax:__ `jpeg_filter_dropon_align top | center | bottom left | center | right`

__Default:__ `center center`

__Context:__ `location`

### jpeg_filter_dropon_offset

__Syntax:__ `jpeg_filter_dropon_offset vertical horizontal`

__Default:__ `0 0`

__Context:__ `location`

### jpeg_filter_dropon

__Syntax:__ `jpeg_filter_dropon image`

__Syntax:__ `jpeg_filter_dropon image mask`

__Default:__ `-`

__Context:__ `location`

## Acknowledgement

This module is heavily inspired by the image filter module with
insights from
["Emiller’s Guide To Nginx Module Development"](https://www.evanmiller.org/nginx-modules-guide.html)
and the
[nginx development guide](https://nginx.org/en/docs/dev/development_guide.html).
