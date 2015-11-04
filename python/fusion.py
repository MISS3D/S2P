# Copyright (C) 2015, Carlo de Franchis <carlo.de-franchis@cmla.ens-cachan.fr>
# Copyright (C) 2015, Gabriele Facciolo <facciolo@cmla.ens-cachan.fr>
# Copyright (C) 2015, Enric Meinhardt <enric.meinhardt@cmla.ens-cachan.fr>
# Copyright (C) 2015, Julien Michel <julien.michel@cnes.fr>

import numpy as np
import common
import piio


def estimate_height_registration(im1,im2):
    """
    Estimate affine registration of heights.
    
    Args:
        im1: first height map
        im2: second height map, to be registered on the first one
    
    Returns
    Offset to apply to second image
    """
    # remove high frequencies with a morphological zoom out
    im1_low_freq = common.image_zoom_out_morpho(im1, 4)
    im2_low_freq = common.image_zoom_out_morpho(im2, 4)
    
    # first read the images and store them as numpy 1D arrays, removing all the
    # nans and inf
    i1 = piio.read(im1_low_freq).ravel() #np.ravel() gives a 1D view
    i2 = piio.read(im2_low_freq).ravel()
    ind = np.logical_and(np.isfinite(i1), np.isfinite(i2))
    h1 = i1[ind]
    h2 = i2[ind]

    # for debug
    print np.shape(i1)
    print np.shape(h1)

#    # 1st option: affine
#    # we search the (u, v) vector that minimizes the following sum (over
#    # all the pixels):
#    #\sum (im1[i] - (u*im2[i]+v))^2
#    # it is a least squares minimization problem
#    A = np.vstack((h2, h2*0+1)).T
#    b = h1
#    z = np.linalg.lstsq(A, b)[0]
#    u = z[0]
#    v = z[1]
#
#    # apply the affine transform and return the modified im2
#    out = common.tmpfile('.tif')
#    common.run('plambda %s "x %f * %f +" > %s' % (im2, u, v, out))

    # 2nd option: translation only
    v = np.mean(h1 - h2)

    return v
    
def merge(im1, im2, im2_offset, thresh, out, conservative=False):
    """
    Args:
        im1, im2: paths to the two input images
        im2_offset: registration offset
        thresh: distance threshold on the intensity values
        out: path to the output image
        conservative (optional, default is False): if True, keep only the
            pixels where the two height map agree

    This function merges two images. They are supposed to be two height maps,
    sampled on the same grid. If a pixel has a valid height (ie not inf) in
    only one of the two maps, then we keep this height (if the 'conservative'
    option is set to False). When two heights are available, if they differ
    less than the threshold we take the mean, if not we discard the pixel (ie
    assign NAN to the output pixel).
    """
    if conservative:
        # then merge
        # the following plambda expression implements:
        # if isfinite x
        #   if isfinite y
        #     if fabs(x - y - offset) < t
        #       return (x + y + offset)/2
        #     return nan
        #   return nan
        # return nan
        common.run("""
            plambda %s %s "x isfinite y isfinite x y - %f - fabs %f < x y + %f + 2 / nan if nan
            if nan if" -o %s
            """ % ( im1, im2, im2_offset, thresh, im2_offset, out))
    else:
        # then merge
        # the following plambda expression implements:
        # if isfinite x
        #   if isfinite y
        #     if fabs(x - y - offset) < t
        #       return (x + y + offset) / 2
        #     return nan
        #   return x
        # return y + offset
        
        common.run("""
        plambda %s %s "x isfinite y isfinite x y - %f - fabs %f < x y + %f + 2 / nan if x
            if y %f + if" -o %s
        """ % ( im1, im2, im2_offset, thresh, im2_offset, im2_offset, out))
