# Copyright (C) 2015, Carlo de Franchis <carlo.de-franchis@cmla.ens-cachan.fr>
# Copyright (C) 2015, Gabriele Facciolo <facciolo@cmla.ens-cachan.fr>
# Copyright (C) 2015, Enric Meinhardt <enric.meinhardt@cmla.ens-cachan.fr>
# Copyright (C) 2015, Julien Michel <julien.michel@cnes.fr>

from __future__ import print_function
import os
import sys
import shutil
import numpy as np
from osgeo import gdal
gdal.UseExceptions()

from s2p import piio
from s2p.config import cfg


def average_if_close(x, threshold):
    """
    """
    if np.nanmax(x) - np.nanmin(x) > threshold:
        return np.nan
    else:
        return np.nanmedian(x)


def merge_n(output, inputs, offsets, averaging='average_if_close', threshold=1):
    """
    Merge n images of equal sizes by taking the median/mean/min/max pixelwise.

    Args:
        inputs: list of paths to the input images
        output: path to the output image
        averaging: string containing the name of a function that accepts
            1D arrays. It is applied to 1D slices of the stack of images along
            the last axis. Possible values are, for instance np.min, np.max,
            np.mean, np.median and their nanproof counterparts, ie np.nanmin,
            np.nanmax, np.nanmean, np.nanmedian
    """
    assert(len(inputs) == len(offsets))

    # get input images size
    if inputs:
        f = gdal.Open(inputs[0])
        w, h = f.RasterXSize, f.RasterYSize
        f = None  # this is the gdal way of closing files

    # read input images and apply offsets
    x = np.empty((h, w, len(inputs)))
    for i, img in enumerate(inputs):
        f = gdal.Open(img)
        x[:, :, i] = f.GetRasterBand(1).ReadAsArray() - offsets[i]
        f = None
        if cfg['debug']:
            piio.write('{}_registered.tif'.format(os.path.splitext(img)[0]),
                       x[:, :, i] + np.mean(offsets))

    # apply the averaging operator
    if averaging.startswith(('np.', 'numpy.')):
        avg = np.apply_along_axis(getattr(sys.modules['numpy'], averaging.split('.')[1]),
                                  axis=2, arr=x)
    elif averaging == 'average_if_close':
        avg = np.apply_along_axis(average_if_close, 2, x, threshold)

    # add the mean offset
    avg += np.mean(offsets)

    # write the average to output
    if inputs:
        shutil.copy(inputs[0], output)  # copy an input file to get the metadata
        f = gdal.Open(output, gdal.GA_Update)
        f.GetRasterBand(1).WriteArray(avg)  # update the output file content
        f = None