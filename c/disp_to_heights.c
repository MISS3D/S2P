#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <math.h>

#include "vvector.h"
#include "iio.h"
#include "rpc.h"
#include "read_matrix.c"

void applyHom(double outv[3], double M[3][3], double v[3]) {
    MAT_DOT_VEC_3X3(outv, M, v);
    outv[0] /= outv[2];
    outv[1] /= outv[2];
    outv[2] /= outv[2];
}

static float interpolate_bilinear(float a, float b, float c, float d,
					float dx, float dy)
{
    double dfx = c-a;
    double dfy = b-a;
    double dfxy = a+d-c-b;
    
    return dfx*dx+dfy*dy+dfxy*dx*dy+a;
}

struct mat3x3 {
    double val[3][3];
};

int main_disp_to_heights(int c, char *v[])
{
    if (c < 12) {
        fprintf(stderr, "usage:\n\t"
                "%s global_out_dir, col, row, tw, th, z, trg_cons, thr_cons, full_outputs, rpc_ref.xml, rpc_slave_1.xml, [rpc_slave_2.xml ...]"
              // 0         1         2    3   4   5   6      7        8          9         10...
                "\n", *v);
        fprintf(stderr,"c = %d\n",c);
        return EXIT_FAILURE;
    }
    

    // read input data
    char tmp_path[1000];
    double det;
    
    char *global_out_dir=v[1];
    int X=atoi(v[2]);
    int Y=atoi(v[3]);
    int width=atoi(v[4]);
    int height=atoi(v[5]);
    double z = atoi(v[6]);
    bool trg_cons = atoi(v[7]);
    double thr_cons = atof(v[8]);
    bool full_outputs = atoi(v[9]);
    
    // build tile_dir path
    char tile_dir[1000];
    sprintf(tile_dir,"%s/tile_%d_%d_row_%d/col_%d",global_out_dir,width,height,Y,X);
    
    // take into account zoom
    width = (int) (width/z)+1;
    height = (int) (height/z)+1;
    
    // read RPC
    struct rpc *initial_rpc_list;
    int N_rpc = c-10;
    initial_rpc_list = (struct rpc *) malloc(N_rpc*sizeof( struct rpc ));
    for(int i=10;i<c;i++)
        read_rpc_file_xml(&initial_rpc_list[i-10], v[i]);
        
    
    // read homographies
    int nb_pairs = N_rpc-1;
    struct mat3x3 *H_ref_list = (struct mat3x3 *) malloc(nb_pairs*sizeof( struct mat3x3 ));
    struct mat3x3 *H_sec_list = (struct mat3x3 *) malloc(nb_pairs*sizeof( struct mat3x3 ));
    for(int i=0;i<nb_pairs;i++)
    {
        sprintf(tmp_path,"%s/pair_%d/H_ref.txt",tile_dir,i+1);
        read_matrix(H_ref_list[i].val, tmp_path); 
    }
    for(int i=0;i<nb_pairs;i++)
    {
        sprintf(tmp_path,"%s/pair_%d/H_sec.txt",tile_dir,i+1);
        read_matrix(H_sec_list[i].val, tmp_path); 
    }
    
    // read global pointing corrections by pair
    struct mat3x3 *A_list = (struct mat3x3 *) malloc(nb_pairs*sizeof( struct mat3x3 ));
    for(int i=0;i<nb_pairs;i++)
    {
        sprintf(tmp_path,"%s/global_pointing_pair_%d.txt",global_out_dir,i+1);
        read_matrix(A_list[i].val, tmp_path); 
    }
    
    // read disparity maps by pair
    int *nx = (int *) malloc(nb_pairs*sizeof( int ));
    int *ny = (int *) malloc(nb_pairs*sizeof( int ));
    int *nch = (int *) malloc(nb_pairs*sizeof( int ));
    
    float **dispx = (float **) malloc(nb_pairs*sizeof( float * ));
    float **dispy = (float **) malloc(nb_pairs*sizeof( float * ));
    for(int i=0;i<nb_pairs;i++)
    {
        sprintf(tmp_path,"%s/pair_%d/rectified_disp.tif",tile_dir,i+1);
        dispx[i] = iio_read_image_float_split(tmp_path, &nx[i], &ny[i], &nch[i]);
        if (nch[i] > 1) dispy[i] = dispx[i] + nx[i]*ny[i];
        else dispy[i] = calloc(nx[i]*ny[i], sizeof(*dispy));
    }
    
    // read mask
    float **mask = (float **) malloc(nb_pairs*sizeof( float * ));
    for(int i=0;i<nb_pairs;i++)
    {
        sprintf(tmp_path,"%s/pair_%d/rectified_mask.png",tile_dir,i+1);
        mask[i] = iio_read_image_float_split(tmp_path, &nx[i], &ny[i], &nch[i]);
    }
    
    // build outputs paths and alloc mem
    
    // * heights
    char fout_heights[1000];
    sprintf(fout_heights,"%s/height_map.tif",tile_dir);
    float *heightMap = (float *) calloc(width*height, sizeof(float));
    
    // * rpc errors
    typedef char tabchar[1000];
    int size_of_fout_err_tab;
    if (full_outputs)   size_of_fout_err_tab = N_rpc+1;
    else            size_of_fout_err_tab = 1;
    
    tabchar *fout_err_tab = (tabchar *) malloc( (size_of_fout_err_tab)*sizeof(tabchar) );
    for(int i=1; i<size_of_fout_err_tab; i++)
        sprintf(fout_err_tab[i],"%s/rpc_err_sight%d.tif",tile_dir,i);
    sprintf(fout_err_tab[0],"%s/rpc_err_all.tif",tile_dir);
    float **errMap_tab = (float **) malloc((size_of_fout_err_tab)*sizeof(float *));
    for(int i=0; i<size_of_fout_err_tab; i++)
        errMap_tab[i] = (float *) calloc(width*height, sizeof(float));
    
    
    // take into account pointing corrections for slave homographies
    struct mat3x3 *H_secB_list = (struct mat3x3 *) malloc(nb_pairs*sizeof( struct mat3x3 ));
    for(int i=0;i<nb_pairs;i++)
    {
        double invA[3][3];
        INVERT_3X3(invA, det, A_list[i].val);
        MATRIX_PRODUCT_3X3(H_secB_list[i].val,H_sec_list[i].val,invA);
    }
    
    // invert slave homographies
    struct mat3x3 *H_invSecB_list = (struct mat3x3 *) malloc(nb_pairs*sizeof( struct mat3x3 ));
    for(int i=0;i<nb_pairs;i++)
        INVERT_3X3(H_invSecB_list[i].val, det, H_secB_list[i].val);
    
    // prepare point correspondances between ref point q0
    // and points q1 from slave images; the list of
    // correspondances, q_list, is built this way :
    // q_list[0] = q0 from ref image
    // q_list[i] = q1 from slave image i.
    // The list of rpc, rpc_list, follows the same sequencing
    double ** q_list = (double **) malloc(N_rpc*sizeof( double * ));
    for(int i=0; i<N_rpc; i++)
        q_list[i] = (double *) malloc(3*sizeof( double ));
        
    struct rpc *rpc_list;
    rpc_list = (struct rpc *) malloc(N_rpc*sizeof( struct rpc ));
    rpc_list[0]=initial_rpc_list[0];
    // note that other rpc will be added only
    // if a view is acceptable; 
    // ref view is always acceptable

    // Precisely, we wish we could know
    // the number of views used for each pixel
    int *nb_views;
    char fnb_views[1000];
    if (full_outputs)
    {
        sprintf(fnb_views,"%s/nb_views.tif",tile_dir);
        nb_views = (int *) calloc(width*height, sizeof(int));
    }

    // We wish we could also know
    // which view have used for each pixel
    // (ref view always selected)
    int *selected_views = (int *) malloc(N_rpc*sizeof(int));
    selected_views[0]=0; 
    
    
    
    // ################################
    // time to build the height map 
    // directly in ref image geometry !
    // ################################
    
    double p0[3]; // a point inside a given disparity map
    double q1[3]; // a point inside a given slave image
    for (int y = 0; y < height; y++) 
        for (int x = 0; x < width; x++) 
        {
            int posH = x + width*y;
            
            // Replace point (x,y) in the height map (h.m.)
            // by its real position inside de ref image:
            // (x,y) --> (x.z+X,y.z+Y)
            double q0[3] = {x*z+X, y*z+Y, 1.};
            
            // push it to q_list
            // (ref view is always acceptable)
            q_list[0][0]=q0[0];
            q_list[0][1]=q0[1];
            q_list[0][2]=q0[2];
            
            // for some reasons, it is possible that
            // a view can't be used; thus, N_views
            // could be decreased suitably
            int N_views = N_rpc;
            int push_position = 1;
            
            for(int i=0;i<nb_pairs;i++) // for each slave image
            {
                // Now, project q0 (from ref image)
                // to the disparity map : p0
                applyHom(p0, H_ref_list[i].val, q0);
                
                // Rounded position inside the disparity map
                int xD=(int) (p0[0]+0.5);
                int yD=(int) (p0[1]+0.5);
                int posD = xD + nx[i]*yD;
                
                // determine whether a view acceptable
                if ( (xD >= 1) && (xD < nx[i]-1) && (yD >= 1) && (yD < ny[i]-1) && (mask[i][posD]>0))
                // (Bilinear interpolation uses a neighborhood of radius 1)
                {

                    // extremal coord. around p0 point
                    int x1=(int) (p0[0]);
                    int y1=(int) (p0[1]);
                    int x2=x1+1;
                    int y2=y1+1;
                    
                    // positions of the four corners around p0 point
                    int posD11 = x1 + nx[i]*y1;
                    int posD12 = x1 + nx[i]*y2;
                    int posD21 = x2 + nx[i]*y1;
                    int posD22 = x2 + nx[i]*y2;
                    
                    // Diff between first corner with floating position
                    double Dx = (double) (p0[0]-x1);
                    double Dy = (double) (p0[1]-y1);
                    
                    // Interpolate disparity map
                    double dx = interpolate_bilinear(dispx[i][posD11],
                                                     dispx[i][posD12],
                                                     dispx[i][posD21],
                                                     dispx[i][posD22],
                                                     Dx,Dy);
                                                     
                    double dy = interpolate_bilinear(dispy[i][posD11],
                                                     dispy[i][posD12],
                                                     dispy[i][posD21],
                                                     dispy[i][posD22],
                                                     Dx,Dy);
                                                     
                    // Map point q0 (from ref image) 
                    // to its mate q1 (from slave image)
                    double p1[3] = {p0[0]+dx, p0[1]+dy, 1.};
                    applyHom(q1, H_invSecB_list[i].val, p1);
                    
                    // put q1 inside q_list;
                    // remember that index 0 is intended
                    // for q0 (point in ref image)
                    // and the rpc of the ref image
                    q_list[push_position][0] = q1[0];
                    q_list[push_position][1] = q1[1];
                    q_list[push_position][2] = q1[2];
                    rpc_list[push_position] = initial_rpc_list[i+1];
                    selected_views[push_position] = i+1;
                    
                    push_position++;
                }
                else
                    N_views--;
            }

            if (N_views>=2) // at least two views are required
            {
                // Altitude
                double hg;
                
                // rpc_height_geo is able to filter out
                // aberrant views (including ref view !)
                int N_views_updated = N_views;
                
                // track errors by view
                double *err = (double *) malloc(N_views*sizeof(double));
                
                // Compute the related height & rpc_err
                hg = rpc_height_geo(rpc_list, q_list, &N_views_updated,
                                    trg_cons, thr_cons, err);
                
                // Error, defined as the mean distance
                // between the optimal point
                // and the set of viewing lines
                if (full_outputs)
                  for(int i=0; i<size_of_fout_err_tab; i++)
                    errMap_tab[i][posH] = NAN;
                    
                double sum = 0.0;
                double nb_elt = 0.0;
                if (N_views_updated>=2)
                {
                    for(int i=0; i<N_views; i++)
                        if ( !isnan(err[i]) )
                        {
                            sum += err[i];
                            nb_elt++;
                            
                            // +1 because index 0 is dedicated
                            // to the mean distance
                            if (full_outputs)
                              errMap_tab[selected_views[i]+1][posH] = err[i];
                        }
                    sum = sum / nb_elt;
                }
                else
                    sum = NAN;
                    
                // Output the result in height & rpc_err maps,
                // both in original geometry
                heightMap[posH] = hg;
                // * index 0 dedicated to mean distance
                errMap_tab[0][posH] = sum;
                if (full_outputs)
                  nb_views[posH] = N_views_updated;
                
                free(err);
            }
            else
            {
                heightMap[posH] = NAN;
                errMap_tab[0][posH] = NAN;
                if (full_outputs)
                    nb_views[posH] = 0;
            } 
        }
    // save the height map / error map / nb_views
    iio_save_image_float_vec(fout_heights, heightMap, width, height, 1);
    for(int i=0;i<size_of_fout_err_tab;i++)
        iio_save_image_float_vec(fout_err_tab[i], errMap_tab[i], width, height, 1);
    if (full_outputs)
        iio_save_image_int(fnb_views, nb_views, width, height);
    
    // clean mem
    free(heightMap);
    if (full_outputs)
        free(nb_views);
    free(initial_rpc_list);
    free(rpc_list);
    free(H_ref_list);
    free(H_sec_list);
    free(H_secB_list);
    free(H_invSecB_list);
    free(A_list);
    free(nx);
    free(ny);
    for(int i=0;i<nb_pairs;i++)
    {
        free(dispx[i]);
        if (nch[i] == 1)
            free(dispy[i]);
        free(mask[i]);
    }
    free(dispx);
    free(dispy);
    free(mask);
    free(nch);
    for(int i=0;i<size_of_fout_err_tab;i++)
        free(errMap_tab[i]);
    free(errMap_tab);
    free(fout_err_tab);
    for(int i=0;i<N_rpc;i++)
        free(q_list[i]);
    free(q_list);
    free(selected_views);
    
    return 0;
}

int main(int c, char *v[])
{
    return main_disp_to_heights(c, v);
}