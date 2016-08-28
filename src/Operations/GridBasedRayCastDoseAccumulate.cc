//GridBasedRayCastDoseAccumulate.cc - A part of DICOMautomaton 2015, 2016. Written by hal clark.

#include <iostream>
#include <sstream>
#include <fstream>
#include <iomanip>
#include <string>    
#include <vector>
#include <set> 
#include <map>
#include <unordered_map>
#include <list>
#include <functional>
#include <thread>
#include <array>
#include <mutex>
#include <limits>
#include <cmath>
#include <regex>

#include <getopt.h>           //Needed for 'getopts' argument parsing.
#include <cstdlib>            //Needed for exit() calls.
#include <utility>            //Needed for std::pair.
#include <algorithm>
#include <experimental/optional>

#include <SFML/Graphics.hpp>
#include <SFML/Audio.hpp>

#include "YgorMisc.h"         //Needed for FUNCINFO, FUNCWARN, FUNCERR macros.
#include "YgorMath.h"         //Needed for vec3 class.
#include "YgorMathPlottingGnuplot.h" //Needed for YgorMathPlottingGnuplot::*.
#include "YgorMathChebyshev.h" //Needed for cheby_approx class.
#include "YgorStats.h"        //Needed for Stats:: namespace.
#include "YgorFilesDirs.h"    //Needed for Does_File_Exist_And_Can_Be_Read(...), etc..
#include "YgorContainers.h"   //Needed for bimap class.
#include "YgorPerformance.h"  //Needed for YgorPerformance_dt_from_last().
#include "YgorAlgorithms.h"   //Needed for For_Each_In_Parallel<..>(...)
#include "YgorArguments.h"    //Needed for ArgumentHandler class.
#include "YgorString.h"       //Needed for GetFirstRegex(...)
#include "YgorImages.h"
#include "YgorImagesIO.h"
#include "YgorImagesPlotting.h"

#include "Explicator.h"       //Needed for Explicator class.

#include "../Structs.h"
#include "../Dose_Meld.h"

#include "../YgorImages_Functors/Grouping/Misc_Functors.h"

#include "../YgorImages_Functors/Processing/DCEMRI_AUC_Map.h"
#include "../YgorImages_Functors/Processing/DCEMRI_S0_Map.h"
#include "../YgorImages_Functors/Processing/DCEMRI_T1_Map.h"
#include "../YgorImages_Functors/Processing/Highlight_ROI_Voxels.h"
#include "../YgorImages_Functors/Processing/Kitchen_Sink_Analysis.h"
#include "../YgorImages_Functors/Processing/IVIMMRI_ADC_Map.h"
#include "../YgorImages_Functors/Processing/Time_Course_Slope_Map.h"
#include "../YgorImages_Functors/Processing/CT_Perfusion_Clip_Search.h"
#include "../YgorImages_Functors/Processing/CT_Perf_Pixel_Filter.h"
#include "../YgorImages_Functors/Processing/CT_Convert_NaNs_to_Air.h"
#include "../YgorImages_Functors/Processing/Min_Pixel_Value.h"
#include "../YgorImages_Functors/Processing/Max_Pixel_Value.h"
#include "../YgorImages_Functors/Processing/CT_Reasonable_HU_Window.h"
#include "../YgorImages_Functors/Processing/Slope_Difference.h"
#include "../YgorImages_Functors/Processing/Centralized_Moments.h"
#include "../YgorImages_Functors/Processing/Logarithmic_Pixel_Scale.h"
#include "../YgorImages_Functors/Processing/Per_ROI_Time_Courses.h"
#include "../YgorImages_Functors/Processing/DBSCAN_Time_Courses.h"
#include "../YgorImages_Functors/Processing/In_Image_Plane_Bilinear_Supersample.h"
#include "../YgorImages_Functors/Processing/In_Image_Plane_Bicubic_Supersample.h"
#include "../YgorImages_Functors/Processing/In_Image_Plane_Pixel_Decimate.h"
#include "../YgorImages_Functors/Processing/Cross_Second_Derivative.h"
#include "../YgorImages_Functors/Processing/Orthogonal_Slices.h"

#include "../YgorImages_Functors/Transform/DCEMRI_C_Map.h"
#include "../YgorImages_Functors/Transform/DCEMRI_Signal_Difference_C.h"
#include "../YgorImages_Functors/Transform/CT_Perfusion_Signal_Diff.h"
#include "../YgorImages_Functors/Transform/DCEMRI_S0_Map_v2.h"
#include "../YgorImages_Functors/Transform/DCEMRI_T1_Map_v2.h"
#include "../YgorImages_Functors/Transform/Pixel_Value_Histogram.h"
#include "../YgorImages_Functors/Transform/Subtract_Spatially_Overlapping_Images.h"

#include "../YgorImages_Functors/Compute/Per_ROI_Time_Courses.h"
#include "../YgorImages_Functors/Compute/Contour_Similarity.h"
#include "../YgorImages_Functors/Compute/AccumulatePixelDistributions.h"

#include "GridBasedRayCastDoseAccumulate.h"



std::list<OperationArgDoc> OpArgDocGridBasedRayCastDoseAccumulate(void){
    std::list<OperationArgDoc> out;

    out.emplace_back();
    out.back().name = "DoseLengthMapFileName";
    out.back().desc = "A filename (or full path) for the (dose)*(length traveled through the ROI peel) image map."
                      " The format is TBD. Leave empty to dump to generate a unique temporary file.";  // TODO: specify format.
    out.back().default_val = "";
    out.back().expected = true;
    out.back().examples = { "", "/tmp/somefile", "localfile.img", "derivative_data.img" };


    out.emplace_back();
    out.back().name = "LengthMapFileName";
    out.back().desc = "A filename (or full path) for the (length traveled through the ROI peel) image map."
                      " The format is TBD. Leave empty to dump to generate a unique temporary file.";  // TODO: specify format.
    out.back().default_val = "";
    out.back().expected = true;
    out.back().examples = { "", "/tmp/somefile", "localfile.img", "derivative_data.img" };


    out.emplace_back();
    out.back().name = "NormalizedROILabelRegex";
    out.back().desc = "A regex matching ROI labels/names to consider. The default will match"
                      " all available ROIs. Be aware that input spaces are trimmed to a single space."
                      " If your ROI name has more than two sequential spaces, use regex to avoid them."
                      " All ROIs have to match the single regex, so use the 'or' token if needed."
                      " Regex is case insensitive and uses extended POSIX syntax.";
    out.back().default_val = ".*";
    out.back().expected = true;
    out.back().examples = { ".*", ".*Body.*", "Body", "Gross_Liver",
                            R"***(.*Left.*Parotid.*|.*Right.*Parotid.*|.*Eye.*)***",
                            R"***(Left Parotid|Right Parotid)***" };

    out.emplace_back();
    out.back().name = "ROILabelRegex";
    out.back().desc = "A regex matching ROI labels/names to consider. The default will match"
                      " all available ROIs. Be aware that input spaces are trimmed to a single space."
                      " If your ROI name has more than two sequential spaces, use regex to avoid them."
                      " All ROIs have to match the single regex, so use the 'or' token if needed."
                      " Regex is case insensitive and uses extended POSIX syntax.";
    out.back().default_val = ".*";
    out.back().expected = true;
    out.back().examples = { ".*", ".*body.*", "body", "Gross_Liver",
                            R"***(.*left.*parotid.*|.*right.*parotid.*|.*eyes.*)***",
                            R"***(left_parotid|right_parotid)***" };

    out.emplace_back();
    out.back().name = "SmallestFeature";
    out.back().desc = "A length giving an estimate of the smallest feature you want to resolve.";
                      " Quantity is in the DICOM coordinate system.";
    out.back().default_val = "0.5";
    out.back().expected = true;
    out.back().examples = { "1.0", "2.0", "0.5", "5.0" };
    
    out.emplace_back();
    out.back().name = "RaydL";
    out.back().desc = "The distance to move a ray each iteration. Should be << img_thickness and << cylinder_radius."
                      " Making too large will invalidate results, causing rays to pass through the surface without"
                      " registering any dose accumulation. Making too small will cause the run-time to grow and may"
                      " eventually lead to truncation or round-off errors. Quantity is in the DICOM coordinate system.";
    out.back().default_val = "0.1";
    out.back().expected = true;
    out.back().examples = { "0.1", "0.05", "0.01", "0.005" };
   

    out.emplace_back();
    out.back().name = "Rows";
    out.back().desc = "The number of rows in the resulting images.";
    out.back().default_val = "256";
    out.back().expected = true;
    out.back().examples = { "10", "50", "128", "1024" };
    
    
    out.emplace_back();
    out.back().name = "Columns";
    out.back().desc = "The number of columns in the resulting images.";
    out.back().default_val = "256";
    out.back().expected = true;
    out.back().examples = { "10", "50", "128", "1024" };

    
    out.emplace_back();
    out.back().name = "NumberOfImages";
    out.back().desc = "The number of images used for grid-based surface detection.";
    out.back().default_val = "20";
    out.back().expected = true;
    out.back().examples = { "10", "50", "100" };
    
    
    return out;
}



Drover GridBasedRayCastDoseAccumulate(Drover DICOM_data, OperationArgPkg OptArgs, std::map<std::string,std::string> /*InvocationMetadata*/, std::string FilenameLex){

    //---------------------------------------------- User Parameters --------------------------------------------------
    auto DoseLengthMapFileName = OptArgs.getValueStr("DoseLengthMapFileName").value();
    auto LengthMapFileName = OptArgs.getValueStr("LengthMapFileName").value();
    const auto ROILabelRegex = OptArgs.getValueStr("ROILabelRegex").value();
    const auto NormalizedROILabelRegex = OptArgs.getValueStr("NormalizedROILabelRegex").value();
    const auto SmallestFeature = std::stod(OptArgs.getValueStr("SmallestFeature").value());
    const auto RaydL = std::stod(OptArgs.getValueStr("RaydL").value());
    const auto Rows = std::stol(OptArgs.getValueStr("Rows").value());
    const auto Columns = std::stol(OptArgs.getValueStr("Columns").value());
    const auto NumberOfImages = std::stol(OptArgs.getValueStr("NumberOfImages").value());

    //-----------------------------------------------------------------------------------------------------------------
    const auto theregex = std::regex(ROILabelRegex, std::regex::icase | std::regex::nosubs | std::regex::optimize | std::regex::extended);
    const auto thenormalizedregex = std::regex(NormalizedROILabelRegex, std::regex::icase | std::regex::nosubs | std::regex::optimize | std::regex::extended);

    Explicator X(FilenameLex);


/*
    //Ensure the Ray dL is sufficiently small. We enforce that ray cannot step over the cylinder in a single iteration
    // for 95% of the width of the cylinder. So if the rays are oncoming and directed at the cylinder perpendicularly,
    // but randomly distributed over the width of the cylinder, then rays will only be able to step over the cylinder
    // without the code 'noticing' 5 times out of every 100 rays. See figure.
    //
    //                  !                       !  
    //                 \!/                     \!/  
    //       -          x                       !                  
    //       |          ! o  o                  x   o  o          -
    //    dL |         o!       o               !o        o       |
    //       |        o !        o              o          o      |   cylinder
    //       -        o x        o              o          o      | diameter/width
    //                 o!       o               xo        o       |
    //                  ! o  o                  !   o  o          -  
    //                  !                       !
    //                  x                       !
    //                  !                       x
    //                 \!/                     \!/
    //                  !                       !
    //
    //              Intersecting Ray       Non-intersecting Ray
    //
    // Note: Glancing rays will be *systematically* lost, but not all glancing rays will be lost. Only some. The amount
    //       lost will depend on the distance from the centre. The probability of a specific ray being lost depends on
    //       the phase relative to the centre.
    //
    // Note: In practice, fewer rays will be lost than predicted if they travel obliquely (not perpendicular) to the
    //       cylinders. The choice of a reasonable (or optimal) ray dL will somewhat depend on the estimated average
    //       obliquity. If there is a lot of obliquity (say, many cylinders are at 45 degrees) then the ray dL can be
    //       relaxed at the same RELATIVE error rate will be achieved. If cylinders are randomly oriented, then the same
    //       is true -- but if you want to control the ABSOLUTE error rate then you are forced to keep ray dL small
    //       enough to handle the perpendicular case. For this reason, we assume the work-case scenario (rays and
    //       cylinders are perpendicular) and hope to get a better-than expected error rate. (But it should not be worse
    //       than we predict!)
    //       
    // Note: Here is how the relationship behaves:
    //       gnuplot> plot 2.0*sqrt(1.0 - x) with impulse ls -1
    //                                                                                                    
    //                                         minRaydL / cyl_radius                                      
    //  0.9 +-+---------------+-----------------+-----------------+-----------------+---------------+-+   
    //      +||+++++          +                 +                 +                 +                 +   
    //      ||||||||++++++                          (minRaydL/cyl_radius) = 2.0*sqrt(1.0 - x) +-----+ |   
    //  0.8 +-+|||||||||||++++                                                                      +-+   
    //      ||||||||||||||||||++++++                                                                  |   
    //      ||||||||||||||||||||||||++++                                                              |   
    //  0.7 +-+|||||||||||||||||||||||||+++++                                                       +-+   
    //      |||||||||||||||||||||||||||||||||+++                                                      |   
    //  0.6 +-+|||||||||||||||||||||||||||||||||+++++                                               +-+   
    //      |||||||||||||||||||||||||||||||||||||||||++++                                             |   
    //      |||||||||||||||||||||||||||||||||||||||||||||+++                                          |   
    //  0.5 +-+|||||||||||||||||||||||||||||||||||||||||||||+++                                     +-+   
    //      |||||||||||||||||||||||||||||||||||||||||||||||||||++++                                   |   
    //      |||||||||||||||||||||||||||||||||||||||||||||||||||||||++                                 |   
    //  0.4 +-+||||||||||||||||||||||||||||||||||||||||||||||||||||||+++                            +-+   
    //      ||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||++                            |   
    //      ||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||++                          |   
    //  0.3 +-+|||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||++                      +-+   
    //      ||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||+                       |   
    //  0.2 +-+||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||++                   +-+   
    //      |||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||+                    |   
    //      ||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||+                   |   
    //  0.1 +-+||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||+                +-+   
    //      ||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||                  |   
    //      +|||||||||||||||||+|||||||||||||||||+|||||||||||||||||+|||||||||||||||||+                 +   
    //    0 +-+---------------+-----------------+-----------------+-----------------+---------------+-+   
    //     0.8               0.85              0.9               0.95               1                1.05 
    //                                    Fraction of rays caught/noticed
    //
    //
    const double catch_frac = 0.95; // Catch 95% of randomly distributed rays incident perpendicularly.
    const double minRaydL = 2.0 * CylinderRadius * std::sqrt(1.0 - catch_frac);
    if(RaydL > minRaydL){
        throw std::runtime_error("Ray dL is too small. MinRaydL=" + std::to_string(minRaydL) + ". Are you sure this is OK? (edit me if so).");
    } 
*/

    //Merge the dose arrays if necessary.
    if(DICOM_data.dose_data.empty()){
        throw std::invalid_argument("This routine requires at least one dose image array. Cannot continue");
    }
    DICOM_data.dose_data = Meld_Dose_Data(DICOM_data.dose_data);
    if(DICOM_data.dose_data.size() != 1){
        throw std::invalid_argument("Unable to meld doses into a single dose array. Cannot continue.");
    }

    //Look for dose data.
    //auto dose_arr_ptr = DICOM_data.image_data.front();
    auto dose_arr_ptr = DICOM_data.dose_data.front();
    if(dose_arr_ptr == nullptr){
        throw std::runtime_error("Encountered a nullptr when expecting a valid Image_Array or Dose_Array ptr.");
    }else if(dose_arr_ptr->imagecoll.images.empty()){
        throw std::runtime_error("Encountered a Image_Array or Dose_Array with valid images -- no images found.");
    }

    //Stuff references to all contours into a list. Remember that you can still address specific contours through
    // the original holding containers (which are not modified here).
    std::list<std::reference_wrapper<contour_collection<double>>> cc_all;
    for(auto & cc : DICOM_data.contour_data->ccs){
        auto base_ptr = reinterpret_cast<contour_collection<double> *>(&cc);
        cc_all.push_back( std::ref(*base_ptr) );
    }

    //Whitelist contours using the provided regex.
    auto cc_ROIs = cc_all;
    cc_ROIs.remove_if([=](std::reference_wrapper<contour_collection<double>> cc) -> bool {
                   const auto ROINameOpt = cc.get().contours.front().GetMetadataValueAs<std::string>("ROIName");
                   const auto ROIName = ROINameOpt.value();
                   return !(std::regex_match(ROIName,theregex));
    });
    cc_ROIs.remove_if([=](std::reference_wrapper<contour_collection<double>> cc) -> bool {
                   const auto ROINameOpt = cc.get().contours.front().GetMetadataValueAs<std::string>("NormalizedROIName");
                   const auto ROIName = ROINameOpt.value();
                   return !(std::regex_match(ROIName,thenormalizedregex));
    });

    if(cc_ROIs.empty()){
        throw std::invalid_argument("No contours selected. Cannot continue.");
    }


    //Find an appropriate unit vector which will define the orientation of a plane parallel to the detector and source grids.
    // Rays will travel parallel to this vector.
    const auto GridZ = vec3<double>(0.0, 0.0, 1.0).unit();

    //Find two more directions (unit vectors) with which to align the bounding box.
    // Note: because we want to be able to compare images from different scans, we use a deterministic 
    // technique for generating two orthogonal directions involving the cardinal directions and Gram-Schmidt
    // orthogonalization.
    vec3<double> GridX = GridZ.rotate_around_z(M_PI * 0.5); // Try Z. Will often be idempotent.
    if(GridX.Dot(GridZ) > 0.25){
        GridX = GridZ.rotate_around_y(M_PI * 0.5);  //Should always work since GridZ is parallel to Z.
    }
    vec3<double> GridY = GridZ.Cross(GridX);
    if(!GridZ.GramSchmidt_orthogonalize(GridX, GridY)){
        throw std::runtime_error("Unable to find grid orientation vectors.");
    }
    GridX = GridX.unit();
    GridY = GridY.unit();


// - If NumberOfImages is 0, make one for each unique contour plane (compared by some small threshold).
//   - If NumberOfImages is negative (e.g., -X), do the same as 0 but make X additional images.
// - If margins

    //Record the unique contour planes (compared by some small threshold) in a sorted list.

    // ...

    //Compute the number of images to make into the grid: number of unique contour planes + 2.

    // ...

    //Figure out what z-margin is needed so the extra two images do not interfere with the grid lining up with the
    //contours. (Want exactly one contour plane per image.)

    // ...

    //Figure out what a reasonable x-margin and y-margin are. (You can use average distance from centroid to vertex as a
    // guide.)

    // ...

    //Generate a grid volume bounding the ROI(s).
    auto grid_image_collection = Contiguously_Grid_Volume<float,double>(
             cc_ROIs, 
             SmallestFeature, SmallestFeature, 10.0*SmallestFeature, //Margins of the bounding volume.
             Rows, Columns, /*number_of_columns=*/ 1, NumberOfImages,
             GridX, GridY, GridZ,
             /*pixel_fill=*/ std::numeric_limits<double>::quiet_NaN(),
             /*only_top_and_bottom=*/ false);
    DICOM_data.image_data.emplace_back( std::make_shared<Image_Array>() );
    DICOM_data.image_data.back()->imagecoll.images.swap(grid_image_collection.images);
    auto grid_arr_ptr = DICOM_data.image_data.back();

return std::move(DICOM_data);

    //Compute the surface mask using the new grid.

    // ...

    const float surface_mask_val = 2.0;

    //Trim geometry above some user-specified plane.
    // ... ideal? necessary? cumbersome? ...


    //Copy the top and bottom images to use as "source" and "detector" images.
    planar_image<float, double> DetectImg = grid_arr_ptr->imagecoll.images.front();
    planar_image<float, double> SourceImg = grid_arr_ptr->imagecoll.images.back();


    //Now ready to ray cast. Loop over integer pixel coordinates. Start and finish are image pixels.
    // The top image can be the length image.
    for(long int row = 0; row < Rows; ++row){
        FUNCINFO("Working on row " << (row+1) << " of " << Rows 
                  << " --> " << static_cast<int>(1000.0*(row+1)/Rows)/10.0 << "\% done");
        for(long int col = 0; col < Columns; ++col){
            double accumulated_length = 0.0;      //Length of ray travel within the 'surface'.
            double accumulated_doselength = 0.0;

            vec3<double> ray_pos = SourceImg.position(row, col);
            const vec3<double> terminus = DetectImg.position(row, col);
            const vec3<double> ray_dir = (terminus - ray_pos).unit();

            //Go until we get within certain distance or overshoot and the ray wants to backtrack.
            while(    (ray_dir.Dot( (terminus - ray_pos).unit() ) > 0.8 ) // Ray orientation is still downward-facing.
                   && (ray_pos.distance(terminus) > std::max(RaydL, SmallestFeature)) ){ // Still far away from detector.

                ray_pos += ray_dir * RaydL;
                const auto midpoint = ray_pos - (ray_dir * RaydL * 0.5);

                //Check if it was in the surface at the midpoint.
                auto rel_img = grid_arr_ptr->imagecoll.get_images_which_encompass_point(midpoint);
                if(rel_img.empty()) continue;
                const auto mask_val = rel_img.front()->value(midpoint, 0);
                const auto is_in_surface = (mask_val == surface_mask_val);
                if(is_in_surface){
                    accumulated_length += RaydL;

                    //Find the dose at the half-way point.
                    auto encompass_imgs = dose_arr_ptr->imagecoll.get_images_which_encompass_point( midpoint );
                    for(const auto &enc_img : encompass_imgs){
                        const auto pix_val = enc_img->value(midpoint, 0);
                        accumulated_doselength += RaydL * pix_val;
                    }
                }
            }

            //Deposit the dose in the images.
            SourceImg.reference(row, col, 0) = static_cast<float>(accumulated_length);
            DetectImg.reference(row, col, 0) = static_cast<float>(accumulated_doselength);
        }
    }

    // Save image maps to file.
    if(!WriteToFITS(SourceImg, LengthMapFileName)){
        throw std::runtime_error("Unable to write FITS file for length map.");
    }
    if(!WriteToFITS(DetectImg, DoseLengthMapFileName)){
        throw std::runtime_error("Unable to write FITS file for dose-length map.");
    }

    return std::move(DICOM_data);
}
