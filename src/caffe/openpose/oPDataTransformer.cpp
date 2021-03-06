#ifdef USE_OPENCV
    #include <opencv2/core/core.hpp>
    // OpenPose: added
    // #include <opencv2/contrib/contrib.hpp>
    // #include <opencv2/contrib/imgproc.hpp>
    // #include <opencv2/highgui/highgui.hpp>
    #include <opencv2/opencv.hpp>
    // OpenPose: added end
#endif  // USE_OPENCV

// OpenPose: added
#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <sstream>
#include <thread>
// OpenPose: added end
#include <string>
#include <vector>

#include "caffe/data_transformer.hpp"
#include "caffe/util/io.hpp"
#include "caffe/util/math_functions.hpp"
#include "caffe/util/rng.hpp"
// OpenPose: added
#include "caffe/util/benchmark.hpp"
#include "caffe/openpose/getLine.hpp"
#include "caffe/openpose/oPDataTransformer.hpp"
// OpenPose: added end

namespace caffe {
// OpenPose: added ended
struct AugmentSelection
{
    bool flip = false;
    std::pair<cv::Mat, cv::Size> RotAndFinalSize;
    cv::Point2i cropCenter;
    float scale = 1.f;
};

void doOcclusions(cv::Mat& imageAugmented, cv::Mat& backgroundImageAugmented, const MetaData& metaData,
                  const unsigned int numberMaxOcclusions, const PoseModel poseModel)
{
    // For all visible keypoints --> [0, numberMaxOcclusions] oclusions
    // For 1/n visible keypoints --> [0, numberMaxOcclusions/n] oclusions
    const float dice = static_cast <float> (rand()) / static_cast <float> (RAND_MAX); //[0,1]
    const auto numberBodyParts = getNumberBodyParts(poseModel);
    int detectedParts = 0;
    for (auto i = 0 ; i < numberBodyParts ; i++)
        if (metaData.jointsSelf.isVisible[i] < 1.5f)
            detectedParts++;
    const auto numberOcclusions = (int)std::round(numberMaxOcclusions * dice * detectedParts / numberBodyParts);
    if (numberOcclusions > 0)
    {
        for (auto i = 0 ; i < numberOcclusions ; i++)
        {
            // Select occluded part
            int occludedPart = -1;
            do
                occludedPart = std::rand() % numberBodyParts; // [0, #BP-1]
            while (metaData.jointsSelf.isVisible[occludedPart] > 1.5f);
            // Select random cropp around it
            const auto width = (int)std::round(imageAugmented.cols * metaData.scaleSelf/2
                             * (1+(std::rand() % 1001 - 500)/1000.)); // +- [0.5-1.5] random
            const auto height = (int)std::round(imageAugmented.rows * metaData.scaleSelf/2
                              * (1+(std::rand() % 1001 - 500)/1000.)); // +- [0.5-1.5] random
            const auto random = 1+(std::rand() % 1001 - 500)/500.; // +- [0-2] random
            // Estimate ROI rectangle to apply
            const auto point = metaData.jointsSelf.points[occludedPart];
            cv::Rect rectangle{(int)std::round(point.x - width/2*random),
                               (int)std::round(point.y - height/2*random), width, height};
            keepRoiInside(rectangle, imageAugmented.size());
            // Apply crop
            if (rectangle.area() > 0)
                backgroundImageAugmented(rectangle).copyTo(imageAugmented(rectangle));
        }
    }
}

void setLabel(cv::Mat& image, const std::string& label, const cv::Point& org)
{
    const int fontface = cv::FONT_HERSHEY_SIMPLEX;
    const double scale = 0.5;
    const int thickness = 1;
    int baseline = 0;
    const cv::Size text = cv::getTextSize(label, fontface, scale, thickness, &baseline);
    cv::rectangle(image, org + cv::Point{0, baseline}, org + cv::Point{text.width, -text.height},
                  cv::Scalar{0,0,0}, CV_FILLED);
    cv::putText(image, label, org, fontface, scale, cv::Scalar{255,255,255}, thickness, 20);
}

void debugVisualize(const cv::Mat& image, const MetaData& metaData, const AugmentSelection& augmentSelection,
                    const PoseModel poseModel, const Phase& phase_, const OPTransformationParameter& param_)
{
    cv::Mat imageToVisualize = image.clone();

    cv::rectangle(imageToVisualize, metaData.objPos-cv::Point2f{3.f,3.f}, metaData.objPos+cv::Point2f{3.f,3.f},
                  cv::Scalar{255,255,0}, CV_FILLED);
    const auto numberBpPafChannels = getNumberBodyAndPafChannels(poseModel);
    for (auto part = 0 ; part < numberBpPafChannels ; part++)
    {
        const auto currentPoint = metaData.jointsSelf.points[part];
        // Hand case
        if (numberBpPafChannels == 21)
        {
            if (part < 4)
                cv::circle(imageToVisualize, currentPoint, 3, cv::Scalar{0,0,255}, -1);
            else if (part < 6 || part == 12 || part == 13)
                cv::circle(imageToVisualize, currentPoint, 3, cv::Scalar{255,0,0}, -1);
            else if (part < 8 || part == 14 || part == 15)
                cv::circle(imageToVisualize, currentPoint, 3, cv::Scalar{255,255,0}, -1);
            else if (part < 10|| part == 16 || part == 17)
                cv::circle(imageToVisualize, currentPoint, 3, cv::Scalar{255,100,0}, -1);
            else if (part < 12|| part == 18 || part == 19)
                cv::circle(imageToVisualize, currentPoint, 3, cv::Scalar{255,100,100}, -1);
            else
                cv::circle(imageToVisualize, currentPoint, 3, cv::Scalar{0,100,100}, -1);
        }
        else if (numberBpPafChannels == 9)
        {
            if (part==0 || part==1 || part==2 || part==6)
                cv::circle(imageToVisualize, currentPoint, 3, cv::Scalar{0,0,255}, -1);
            else if (part==3 || part==4 || part==5 || part==7)
                cv::circle(imageToVisualize, currentPoint, 3, cv::Scalar{255,0,0}, -1);
            else
                cv::circle(imageToVisualize, currentPoint, 3, cv::Scalar{255,255,0}, -1);
        }
        // Body case (CPM)
        else if (numberBpPafChannels == 14 || numberBpPafChannels == 28)
        {
            if (part < 14)
            {
                if (part==2 || part==3 || part==4 || part==8 || part==9 || part==10)
                    cv::circle(imageToVisualize, currentPoint, 3, cv::Scalar{0,0,255}, -1);
                else if (part==5 || part==6 || part==7 || part==11 || part==12 || part==13)
                    cv::circle(imageToVisualize, currentPoint, 3, cv::Scalar{255,0,0}, -1);
                else
                    cv::circle(imageToVisualize, currentPoint, 3, cv::Scalar{255,255,0}, -1);
            }
            else if (part < 16)
                cv::circle(imageToVisualize, currentPoint, 3, cv::Scalar{0,255,0}, -1);
            else
            {
                if (part==17 || part==18 || part==19 || part==23 || part==24)
                    cv::circle(imageToVisualize, currentPoint, 3, cv::Scalar{255,0,0}, -1);
                else if (part==20 || part==21 || part==22 || part==25 || part==26)
                    cv::circle(imageToVisualize, currentPoint, 3, cv::Scalar{255,100,100}, -1);
                else
                    cv::circle(imageToVisualize, currentPoint, 3, cv::Scalar{255,200,200}, -1);
            }
        }
        else
        {
            if (metaData.jointsSelf.isVisible[part] <= 1)
                cv::circle(imageToVisualize, currentPoint, 3, cv::Scalar{200,200,255}, -1);
        }
    }

    cv::line(imageToVisualize, metaData.objPos + cv::Point2f{-368/2.f,-368/2.f},
             metaData.objPos + cv::Point2f{368/2.f,-368/2.f}, cv::Scalar{0,255,0}, 2);
    cv::line(imageToVisualize, metaData.objPos + cv::Point2f{368/2.f,-368/2.f},
             metaData.objPos + cv::Point2f{368/2.f,368/2.f}, cv::Scalar{0,255,0}, 2);
    cv::line(imageToVisualize, metaData.objPos + cv::Point2f{368/2.f,368/2.f},
             metaData.objPos + cv::Point2f{-368/2.f,368/2.f}, cv::Scalar{0,255,0}, 2);
    cv::line(imageToVisualize, metaData.objPos + cv::Point2f{-368/2.f,368/2.f},
             metaData.objPos + cv::Point2f{-368/2.f,-368/2.f}, cv::Scalar{0,255,0}, 2);

    for (auto person=0;person<metaData.numberOtherPeople;person++)
    {
        cv::rectangle(imageToVisualize,
                      metaData.objPosOthers[person]-cv::Point2f{3.f,3.f},
                      metaData.objPosOthers[person]+cv::Point2f{3.f,3.f}, cv::Scalar{0,255,255}, CV_FILLED);
        for (auto part = 0 ; part < numberBpPafChannels ; part++)
            if (metaData.jointsOthers[person].isVisible[part] <= 1)
                cv::circle(imageToVisualize, metaData.jointsOthers[person].points[part], 3, cv::Scalar{0,0,255}, -1);
    }

    // Draw text
    char imagename [100];
    if (phase_ == TRAIN)
    {
        std::stringstream ss;
        // ss << "Augmenting with:" << (augmentSelection.flip ? "flip" : "no flip")
        //    << "; Rotate " << augmentSelection.RotAndFinalSize.first << " deg; scaling: "
        //    << augmentSelection.scale << "; crop: " << augmentSelection.cropCenter.height
        //    << "," << augmentSelection.cropCenter.width;
        ss << metaData.datasetString << " " << metaData.writeNumber << " index:" << metaData.annotationListIndex
           << "; person:" << metaData.peopleIndex << "; o_scale: " << metaData.scaleSelf;
        std::string stringInfo = ss.str();
        setLabel(imageToVisualize, stringInfo, cv::Point{0, 20});

        std::stringstream ss2;
        ss2 << "mult: " << augmentSelection.scale << "; rot: " << augmentSelection.RotAndFinalSize.first << "; flip: "
            << (augmentSelection.flip?"true":"ori");
        stringInfo = ss2.str();
        setLabel(imageToVisualize, stringInfo, cv::Point{0, 40});

        cv::rectangle(imageToVisualize, cv::Point{0, (int)(imageToVisualize.rows)},
                      cv::Point{(int)(param_.crop_size_x()), (int)(param_.crop_size_y()+imageToVisualize.rows)},
                      cv::Scalar{255,255,255}, 1);

        sprintf(imagename, "visualize/augment_%04d_epoch_%03d_writenum_%03d.jpg", metaData.writeNumber,
                metaData.epoch, metaData.writeNumber);
    }
    else
    {
        const std::string stringInfo = "no augmentation for testing";
        setLabel(imageToVisualize, stringInfo, cv::Point{0, 20});

        sprintf(imagename, "visualize/augment_%04d.jpg", metaData.writeNumber);
    }
    //LOG(INFO) << "filename is " << imagename;
    cv::imwrite(imagename, imageToVisualize);
}
template<typename Dtype>
int getType(Dtype dtype)
{
    (void)dtype;
    if (sizeof(Dtype) == sizeof(float))
        return CV_32F;
    else if (sizeof(Dtype) == sizeof(double))
        return CV_64F;
    else
    {
        throw std::runtime_error{"Only float or double" + getLine(__LINE__, __FUNCTION__, __FILE__)};
        return CV_32F;
    }
}
// OpenPose: added ended

template<typename Dtype>
OPDataTransformer<Dtype>::OPDataTransformer(const OPTransformationParameter& param,
        Phase phase, const std::string& modelString) // OpenPose: Added std::string
        // : param_(param), phase_(phase) {
        : param_(param), phase_(phase), mCurrentEpoch{-1} {
    // OpenPose: commented
    // // check if we want to use mean_file
    // if (param_.has_mean_file()) {
    //     CHECK_EQ(param_.mean_value_size(), 0) <<
    //         "Cannot specify mean_file and mean_value at the same time";
    //     const std::string& mean_file = param.mean_file();
    //     if (Caffe::root_solver()) {
    //         LOG(INFO) << "Loading mean file from: " << mean_file;
    //     }
    //     BlobProto blob_proto;
    //     ReadProtoFromBinaryFileOrDie(mean_file.c_str(), &blob_proto);
    //     data_mean_.FromProto(blob_proto);
    // }
    // // check if we want to use mean_value
    // if (param_.mean_value_size() > 0) {
    //     CHECK(param_.has_mean_file() == false) <<
    //         "Cannot specify mean_file and mean_value at the same time";
    //     for (int c = 0; c < param_.mean_value_size(); ++c) {
    //         mean_values_.push_back(param_.mean_value(c));
    //     }
    // }
    // OpenPose: commented end
    // OpenPose: added
    LOG(INFO) << "OPDataTransformer constructor done.";
    // PoseModel
    std::tie(mPoseModel, mPoseCategory) = flagsToPoseModel(modelString);
    mModelString = modelString;
    // OpenPose: added end
}

// OpenPose: commented
// template <typename Dtype>
// void OPDataTransformer<Dtype>::InitRand() {
//     const bool needs_rand = param_.mirror() ||
//             (phase_ == TRAIN && param_.crop_size());
//     if (needs_rand)
//     {
//         const unsigned int rng_seed = caffe_rng_rand();
//         rng_.reset(new Caffe::RNG(rng_seed));
//     }
//     else
//         rng_.reset();
// }

// OpenPose: added
template<typename Dtype>
void OPDataTransformer<Dtype>::Transform(Blob<Dtype>* transformedData, Blob<Dtype>* transformedLabel,
                                         const Datum& datum, const Datum* datumNegative)
{
    // Secuirty checks
    const int datumChannels = datum.channels();
    const int imageNum = transformedData->num();
    const int imageChannels = transformedData->channels();
    const int labelNum = transformedLabel->num();
    CHECK_GE(datumChannels, 1);
    CHECK_EQ(imageChannels, 3);
    CHECK_EQ(imageNum, labelNum);
    CHECK_GE(imageNum, 1);

    auto* transformedDataPtr = transformedData->mutable_cpu_data();
    auto* transformedLabelPtr = transformedLabel->mutable_cpu_data();
    CPUTimer timer;
    timer.Start();
    generateDataAndLabel(transformedDataPtr, transformedLabelPtr, datum, datumNegative);
    VLOG(2) << "Transform: " << timer.MicroSeconds() / 1000.0  << " ms";
}

template <typename Dtype>
int OPDataTransformer<Dtype>::getNumberChannels() const
{
    return 2 * getNumberBodyBkgAndPAF(mPoseModel);
    // // For Distance
    // return 2 * (getNumberBodyBkgAndPAF(mPoseModel) + getNumberPafChannels(mPoseModel)/2);
}
// OpenPose: end

// OpenPose: commented
// template <typename Dtype>
// int OPDataTransformer<Dtype>::Rand(int n) {
//     CHECK(rng_);
//     CHECK_GT(n, 0);
//     caffe::rng_t* rng = static_cast<caffe::rng_t*>(rng_->generator());
//     return ((*rng)() % n);
// }

// OpenPose: added
template<typename Dtype>
void OPDataTransformer<Dtype>::generateDataAndLabel(Dtype* transformedData, Dtype* transformedLabel,
                                                    const Datum& datum, const Datum* datumNegative)
{
    // Parameters
    const std::string& data = datum.data();
    const int datumHeight = datum.height();
    const int datumWidth = datum.width();
    const auto datumArea = (int)(datumHeight * datumWidth);
    const cv::Size finalCropSize{(int)param_.crop_size_x(), (int)param_.crop_size_y()};
    const auto stride = (int)param_.stride();
    const auto finalImageWidth = (int)param_.crop_size_x();
    const auto finalImageHeight = (int)param_.crop_size_y();
    const auto gridX = finalImageWidth / stride;
    const auto gridY = finalImageHeight / stride;

    // Time measurement
    CPUTimer timer1;
    timer1.Start();

    // const bool hasUInt8 = data.size() > 0;
    CHECK(data.size() > 0);

    // Read meta data (LMDB channel 3)
    MetaData metaData;
    // DOME
    if (mPoseCategory == PoseCategory::DOME)
        readMetaData<Dtype>(metaData, mCurrentEpoch, data.c_str(), datumWidth, mPoseCategory, mPoseModel);
    // COCO & MPII
    else
        readMetaData<Dtype>(metaData, mCurrentEpoch, &data[3 * datumArea], datumWidth, mPoseCategory, mPoseModel);
    const auto depthEnabled = metaData.depthEnabled;

    // Read image (LMDB channel 1)
    cv::Mat image;
    // DOME
    if (mPoseCategory == PoseCategory::DOME)
    {
        const auto imageFullPath = param_.media_directory() + metaData.imageSource;
        image = cv::imread(imageFullPath, CV_LOAD_IMAGE_COLOR);
        if (image.empty())
            throw std::runtime_error{"Empty image at " + imageFullPath + getLine(__LINE__, __FUNCTION__, __FILE__)};
    }
    // COCO & MPII
    else
    {
        // // Naive copy
        // image = cv::Mat(datumHeight, datumWidth, CV_8UC3);
        // const auto initImageArea = (int)(image.rows * image.cols);
        // CHECK_EQ(initImageArea, datumArea);
        // for (auto y = 0; y < image.rows; y++)
        // {
        //     const auto yOffset = (int)(y*image.cols);
        //     for (auto x = 0; x < image.cols; x++)
        //     {
        //         const auto xyOffset = yOffset + x;
        //         cv::Vec3b& bgr = image.at<cv::Vec3b>(y, x);
        //         for (auto c = 0; c < 3; c++)
        //         {
        //             const auto dIndex = (int)(c*initImageArea + xyOffset);
        //             // if (hasUInt8)
        //                 bgr[c] = static_cast<Dtype>(static_cast<uint8_t>(data[dIndex]));
        //             // else
        //                 // bgr[c] = datum.float_data(dIndex);
        //         }
        //     }
        // }
        // // Naive copy (slightly optimized)
        // image = cv::Mat(datumHeight, datumWidth, CV_8UC3);
        // auto* uCharPtrCvMat = (unsigned char*)(image.data);
        // for (auto y = 0; y < image.rows; y++)
        // {
        //     const auto yOffset = (int)(y*image.cols);
        //     for (auto x = 0; x < image.cols; x++)
        //     {
        //         const auto xyOffset = yOffset + x;
        //         const auto baseIndex = 3*xyOffset;
        //         uCharPtrCvMat[baseIndex] = static_cast<Dtype>(static_cast<uint8_t>(data[xyOffset]));
        //         uCharPtrCvMat[baseIndex + 1] = static_cast<Dtype>(static_cast<uint8_t>(data[xyOffset + initImageArea]));
        //         uCharPtrCvMat[baseIndex + 2] = static_cast<Dtype>(static_cast<uint8_t>(data[xyOffset + 2*initImageArea]));
        //     }
        // }
        // // Security check - Assert
        // cv::Mat image2;
        // std::swap(image, image2);
        // OpenCV wrapping --> 1.7x speed up naive image.at<cv::Vec3b>, 1.25x speed up with smart speed up
        const cv::Mat b(datumHeight, datumWidth, CV_8UC1, (unsigned char*)&data[0]);
        const cv::Mat g(datumHeight, datumWidth, CV_8UC1, (unsigned char*)&data[datumArea]);
        const cv::Mat r(datumHeight, datumWidth, CV_8UC1, (unsigned char*)&data[2*datumArea]);
        cv::merge({b,g,r}, image);
        // // Security checks
        // const auto initImageArea = (int)(image.rows * image.cols);
        // CHECK_EQ(initImageArea, datumArea);
        // CHECK_EQ(cv::norm(image-image2), 0);
    }
    const auto initImageWidth = (int)image.cols;
    const auto initImageHeight = (int)image.rows;

    // Read background image
    cv::Mat backgroundImage;
    if (datumNegative != nullptr)
    {
        const std::string& data = datumNegative->data();
        const int datumNegativeWidth = datumNegative->width();
        const int datumNegativeHeight = datumNegative->height();
        const auto datumNegativeArea = (int)(datumNegativeHeight * datumNegativeWidth);
        // Background image
        // // Naive copy
        // backgroundImage = cv::Mat(datumNegativeHeight, datumNegativeWidth, CV_8UC3);
        // for (auto y = 0; y < datumNegativeHeight; y++)
        // {
        //     const auto yOffset = (int)(y*datumNegativeWidth);
        //     for (auto x = 0; x < datumNegativeWidth; x++)
        //     {
        //         const auto xyOffset = yOffset + x;
        //         cv::Vec3b& bgr = backgroundImage.at<cv::Vec3b>(y, x);
        //         for (auto c = 0; c < 3; c++)
        //         {
        //             const auto dIndex = (int)(c*datumNegativeArea + xyOffset);
        //             bgr[c] = static_cast<Dtype>(static_cast<uint8_t>(data[dIndex]));
        //         }
        //     }
        // }
        // // Naive copy (slightly optimized)
        // backgroundImage = cv::Mat(datumNegativeHeight, datumNegativeWidth, CV_8UC3);
        // auto* uCharPtrCvMat = (unsigned char*)(backgroundImage.data);
        // for (auto y = 0; y < datumNegativeHeight; y++)
        // {
        //     const auto yOffset = (int)(y*datumNegativeWidth);
        //     for (auto x = 0; x < datumNegativeWidth; x++)
        //     {
        //         const auto xyOffset = yOffset + x;
        //         const auto baseIndex = 3*xyOffset;
        //         uCharPtrCvMat[baseIndex] = static_cast<Dtype>(static_cast<uint8_t>(data[xyOffset]));
        //         uCharPtrCvMat[baseIndex + 1] = static_cast<Dtype>(static_cast<uint8_t>(data[xyOffset + datumNegativeArea]));
        //         uCharPtrCvMat[baseIndex + 2] = static_cast<Dtype>(static_cast<uint8_t>(data[xyOffset + 2*datumNegativeArea]));
        //     }
        // }
        // // Security check - Assert
        // cv::Mat image2;
        // std::swap(backgroundImage, image2);
        // OpenCV wrapping --> 1.7x speed up naive image.at<cv::Vec3b>, 1.25x speed up with smart speed up
        const cv::Mat b(datumNegativeHeight, datumNegativeWidth, CV_8UC1, (uchar*)&data[0]);
        const cv::Mat g(datumNegativeHeight, datumNegativeWidth, CV_8UC1, (uchar*)&data[datumNegativeArea]);
        const cv::Mat r(datumNegativeHeight, datumNegativeWidth, CV_8UC1, (uchar*)&data[2*datumNegativeArea]);
        cv::merge({b,g,r}, backgroundImage);
        // // Security checks
        // const auto datumNegativeArea2 = (int)(backgroundImage.rows * backgroundImage.cols);
        // CHECK_EQ(datumNegativeArea2, datumNegativeArea);
        // CHECK_EQ(cv::norm(backgroundImage-image2), 0);
        // Included data augmentation: cropping
        // Disable data augmentation --> minX = minY = 0
        // Data augmentation: cropping
        if (datumNegativeWidth > finalImageWidth && datumNegativeHeight > finalImageHeight)
        {
            const auto xDiff = datumNegativeWidth - finalImageWidth;
            const auto yDiff = datumNegativeHeight - finalImageHeight;
            const auto minX = (xDiff <= 0 ? 0 :
                (int)std::round(xDiff * float(std::rand()) / float(RAND_MAX)) // [0,1]
            );
            const auto minY = (xDiff <= 0 ? 0 :
                (int)std::round(yDiff * float(std::rand()) / float(RAND_MAX)) // [0,1]
            );
            cv::Mat backgroundImageTemp;
            std::swap(backgroundImage, backgroundImageTemp);
            const cv::Point2i backgroundCropCenter{minX + finalImageWidth/2, minY + finalImageHeight/2};
            applyCrop(backgroundImage, backgroundCropCenter, backgroundImageTemp, 0, finalCropSize);
        }
        // Resize (if smaller than final crop size)
        // if (datumNegativeWidth < finalImageWidth || datumNegativeHeight < finalImageHeight)
        else
        {
            cv::Mat backgroundImageTemp;
            std::swap(backgroundImage, backgroundImageTemp);
            cv::resize(backgroundImageTemp, backgroundImage, cv::Size{finalImageWidth, finalImageHeight}, 0, 0, CV_INTER_CUBIC);
        }
    }

    // Read mask miss (LMDB channel 2)
    const cv::Mat maskMiss = (mPoseCategory == PoseCategory::COCO
        // COCO
        ? cv::Mat(initImageHeight, initImageWidth, CV_8UC1, (unsigned char*)&data[4*datumArea])
        // DOME & MPII
        : cv::Mat(initImageHeight, initImageWidth, CV_8UC1, cv::Scalar{255}));
    // // Naive copy
    // cv::Mat maskMiss2;
    // // COCO
    // if (mPoseCategory == PoseCategory::COCO)
    // {
    //     maskMiss2 = cv::Mat(initImageHeight, initImageWidth, CV_8UC1, cv::Scalar{0});
    //     for (auto y = 0; y < maskMiss2.rows; y++)
    //     {
    //         const auto yOffset = (int)(y*initImageWidth);
    //         for (auto x = 0; x < initImageWidth; x++)
    //         {
    //             const auto xyOffset = yOffset + x;
    //             const auto dIndex = (int)(4*datumArea + xyOffset);
    //             Dtype dElement;
    //             // if (hasUInt8)
    //                 dElement = static_cast<Dtype>(static_cast<uint8_t>(data[dIndex]));
    //             // else
    //                 // dElement = datum.float_data(dIndex);
    //             if (std::round(dElement/255)!=1 && std::round(dElement/255)!=0)
    //                 throw std::runtime_error{"Value out of {0,1}" + getLine(__LINE__, __FUNCTION__, __FILE__)};
    //             maskMiss2.at<uchar>(y, x) = dElement; //round(dElement/255);
    //         }
    //     }
    // }
    // // DOME & MPII
    // else
    //     maskMiss2 = cv::Mat(initImageHeight, initImageWidth, CV_8UC1, cv::Scalar{255});
    // // Security checks
    // CHECK_EQ(cv::norm(maskMiss-maskMiss2), 0);

    // Time measurement
    VLOG(2) << "  bgr[:] = datum: " << timer1.MicroSeconds()*1e-3 << " ms";

    // Depth image
    cv::Mat depth;
    if (depthEnabled)
    {
        const auto depthFullPath = param_.media_directory() + metaData.depthSource;
        depth = cv::imread(depthFullPath, CV_LOAD_IMAGE_ANYDEPTH);
        if (image.empty())
            throw std::runtime_error{"Empty depth at " + depthFullPath + getLine(__LINE__, __FUNCTION__, __FILE__)};
    }

    // timer1.Start();
    // // Clahe
    // if (param_.do_clahe())
    //     clahe(image, param_.clahe_tile_size(), param_.clahe_clip_limit());
    // BGR --> Gray --> BGR
    // if image is grey
    // cv::cvtColor(image, image, CV_GRAY2BGR);
    // VLOG(2) << "  cvtColor and CLAHE: " << timer1.MicroSeconds()*1e-3 << " ms";
    timer1.Start();

    VLOG(2) << "  ReadMeta+MetaJoints: " << timer1.MicroSeconds()*1e-3 << " ms";

    // Data augmentation
    timer1.Start();
    AugmentSelection augmentSelection;
    // // Debug - Visualize original
    // debugVisualize(image, metaData, augmentSelection, mPoseModel, phase_, param_);
    // Augmentation
    cv::Mat imageAugmented;
    cv::Mat backgroundImageAugmented;
    cv::Mat maskMissAugmented;
    cv::Mat depthAugmented;
    VLOG(2) << "   input size (" << initImageWidth << ", " << initImageHeight << ")";
    // We only do random transform augmentSelection augmentation when training.
    if (phase_ == TRAIN) // 80% time is spent here
    {
        // Mask for background image
        // Image size, not backgroundImage
        cv::Mat maskBackgroundImage = (datumNegative != nullptr
            ? cv::Mat(initImageHeight, initImageWidth, CV_8UC1, cv::Scalar{0}) : cv::Mat());
        cv::Mat maskBackgroundImageAugmented;
        // Swap center?
        swapCenterPoint(metaData, param_, mPoseModel);
        // Augmentation (scale, rotation, cropping, and flipping)
        // Order does matter, otherwise code will fail doing augmentation
        augmentSelection.scale = estimateScale(metaData, param_);
        applyScale(metaData, augmentSelection.scale, mPoseModel);
        augmentSelection.RotAndFinalSize = estimateRotation(
            metaData,
            cv::Size{(int)std::round(image.cols * augmentSelection.scale),
                     (int)std::round(image.rows * augmentSelection.scale)},
            param_);
        applyRotation(metaData, augmentSelection.RotAndFinalSize.first, mPoseModel);
        augmentSelection.cropCenter = estimateCrop(metaData, param_);
        applyCrop(metaData, augmentSelection.cropCenter, finalCropSize, mPoseModel);
        augmentSelection.flip = estimateFlip(metaData, param_);
        applyFlip(metaData, augmentSelection.flip, finalImageHeight, param_, mPoseModel);
        // Aug on images - ~80% code time spent in the following `applyAllAugmentation` lines
        applyAllAugmentation(imageAugmented, augmentSelection.RotAndFinalSize.first, augmentSelection.scale,
                             augmentSelection.flip, augmentSelection.cropCenter, finalCropSize, image,
                             0);
        applyAllAugmentation(maskBackgroundImageAugmented, augmentSelection.RotAndFinalSize.first,
                             augmentSelection.scale, augmentSelection.flip, augmentSelection.cropCenter,
                             finalCropSize, maskBackgroundImage, 255);
        applyAllAugmentation(maskMissAugmented, augmentSelection.RotAndFinalSize.first,
                             augmentSelection.scale, augmentSelection.flip, augmentSelection.cropCenter,
                             finalCropSize, maskMiss, 255);
        applyAllAugmentation(depthAugmented, augmentSelection.RotAndFinalSize.first,
                             augmentSelection.scale, augmentSelection.flip, augmentSelection.cropCenter,
                             finalCropSize, depth, 0);
        // backgroundImage augmentation (no scale/rotation)
        const cv::Point2i backgroundCropCenter{backgroundImage.cols/2, backgroundImage.rows/2};
        cv::Mat backgroundImageTemp;
        applyCrop(backgroundImageTemp, backgroundCropCenter, backgroundImage, 0, finalCropSize);
        applyFlip(backgroundImageAugmented, augmentSelection.flip, backgroundImageTemp);
        // Introduce occlusions
        doOcclusions(imageAugmented, backgroundImageAugmented, metaData, param_.number_max_occlusions(),
                     mPoseModel);
        // Resize mask
        if (!maskMissAugmented.empty())
            cv::resize(maskMissAugmented, maskMissAugmented, cv::Size{gridX, gridY}, 0, 0, cv::INTER_AREA);
        // Final background image - elementwise multiplication
        if (!backgroundImageAugmented.empty() && !maskBackgroundImageAugmented.empty())
        {
            // Apply mask to background image
            cv::Mat backgroundImageAugmentedTemp;
            backgroundImageAugmented.copyTo(backgroundImageAugmentedTemp, maskBackgroundImageAugmented);
            // Add background image to image augmented
            cv::Mat imageAugmentedTemp;
            addWeighted(imageAugmented, 1., backgroundImageAugmentedTemp, 1., 0., imageAugmentedTemp);
            imageAugmented = imageAugmentedTemp;
        }
        if (depthEnabled && !depthAugmented.empty())
            cv::resize(depthAugmented, depthAugmented, cv::Size{gridX, gridY}, 0, 0, cv::INTER_AREA);
    }
    // Test
    else
    {
        imageAugmented = image;
        maskMissAugmented = maskMiss;
        depthAugmented = depth;
        // Resize mask
        if (!maskMissAugmented.empty())
            cv::resize(maskMissAugmented, maskMissAugmented, cv::Size{gridX, gridY}, 0, 0, cv::INTER_AREA);
        if (depthEnabled)
            cv::resize(depthAugmented, depthAugmented, cv::Size{gridX, gridY}, 0, 0, cv::INTER_AREA);
    }
    // // Debug - Visualize final (augmented) image
    // debugVisualize(imageAugmented, metaData, augmentSelection, mPoseModel, phase_, param_);
    // Augmentation time
    VLOG(2) << "  Aug: " << timer1.MicroSeconds()*1e-3 << " ms";
    // Data copy
    timer1.Start();
    // Copy imageAugmented into transformedData + mean-subtraction
    const int imageAugmentedArea = imageAugmented.rows * imageAugmented.cols;
    auto* uCharPtrCvMat = (unsigned char*)(imageAugmented.data);
    // x/256 - 0.5
    if (param_.normalization() == 0)
    {
        for (auto y = 0; y < imageAugmented.rows; y++)
        {
            const auto yOffset = y*imageAugmented.cols;
            for (auto x = 0; x < imageAugmented.cols; x++)
            {
                const auto xyOffset = yOffset + x;
                // const cv::Vec3b& bgr = imageAugmented.at<cv::Vec3b>(y, x);
                auto* bgr = &uCharPtrCvMat[3*xyOffset];
                transformedData[xyOffset] = (bgr[0] - 128) / 256.0;
                transformedData[xyOffset + imageAugmentedArea] = (bgr[1] - 128) / 256.0;
                transformedData[xyOffset + 2*imageAugmentedArea] = (bgr[2] - 128) / 256.0;
            }
        }
    }
    // x - channel average
    else if (param_.normalization() == 1)
    {
        for (auto y = 0; y < imageAugmented.rows ; y++)
        {
            const auto yOffset = y*imageAugmented.cols;
            for (auto x = 0; x < imageAugmented.cols ; x++)
            {
                const auto xyOffset = yOffset + x;
                // const cv::Vec3b& bgr = imageAugmented.at<cv::Vec3b>(y, x);
                auto* bgr = &uCharPtrCvMat[3*xyOffset];
                transformedData[xyOffset] = bgr[0] - 102.9801;
                transformedData[xyOffset + imageAugmentedArea] = bgr[1] - 115.9465;
                transformedData[xyOffset + 2*imageAugmentedArea] = bgr[2] - 122.7717;
            }
        }
    }
    // Unknown
    else
        throw std::runtime_error{"Unknown normalization at " + getLine(__LINE__, __FUNCTION__, __FILE__)};

    // Generate and copy label
    generateLabelMap(transformedLabel, imageAugmented.size(), maskMissAugmented, metaData);
    if (depthEnabled)
        generateDepthLabelMap(transformedLabel, depthAugmented);
    VLOG(2) << "  AddGaussian+CreateLabel: " << timer1.MicroSeconds()*1e-3 << " ms";

    // // Debugging - Visualize - Write on disk
    // // if (mPoseModel == PoseModel::DOME_59)
    // {
    //     // if (metaData.writeNumber < 5)
    //     if (metaData.writeNumber < 10)
    //     // if (metaData.writeNumber < 100)
    //     {
    //         // 1. Create `visualize` folder in training folder (where train_pose.sh is located)
    //         // 2. Comment the following if statement
    //         const auto rezX = (int)imageAugmented.cols;
    //         const auto rezY = (int)imageAugmented.rows;
    //         const auto gridX = rezX / stride;
    //         const auto gridY = rezY / stride;
    //         const auto channelOffset = gridY * gridX;
    //         const auto numberTotalChannels = getNumberBodyBkgAndPAF(mPoseModel);
    //         for (auto part = 0; part < numberTotalChannels; part++)
    //         {
    //             // Reduce #images saved (ideally mask images should be the same)
    //             // if (part < 1)
    //             if (part == numberTotalChannels-1)
    //             // if (part < 3 || part >= numberTotalChannels - 3)
    //             {
    //                 cv::Mat finalImage = cv::Mat::zeros(gridY, 2*gridX, CV_8UC1);
    //                 for (auto subPart = 0; subPart < 2; subPart++)
    //                 {
    //                     cv::Mat labelMap = finalImage(cv::Rect{subPart*gridX, 0, gridX, gridY});
    //                     for (auto gY = 0; gY < gridY; gY++)
    //                     {
    //                         const auto yOffset = gY*gridX;
    //                         for (auto gX = 0; gX < gridX; gX++)
    //                         {
    //                             const auto channelIndex = (part+numberTotalChannels*subPart)*channelOffset;
    //                             labelMap.at<uchar>(gY,gX) = (int)(255*transformedLabel[channelIndex + yOffset + gX]);
    //                         }
    //                     }
    //                 }
    //                 cv::resize(finalImage, finalImage, cv::Size{}, stride, stride, cv::INTER_LINEAR);
    //                 cv::applyColorMap(finalImage, finalImage, cv::COLORMAP_JET);
    //                 for (auto subPart = 0; subPart < 2; subPart++)
    //                 {
    //                     cv::Mat labelMap = finalImage(cv::Rect{subPart*rezX, 0, rezX, rezY});
    //                     cv::addWeighted(labelMap, 0.5, imageAugmented, 0.5, 0.0, labelMap);
    //                 }
    //                 // Write on disk
    //                 char imagename [100];
    //                 sprintf(imagename, "visualize/%s_augment_%04d_label_part_%02d.jpg", mModelString.c_str(),
    //                         metaData.writeNumber, part);
    //                 cv::imwrite(imagename, finalImage);
    //             }
    //         }
    //         if (depthEnabled)
    //         {
    //             cv::Mat depthMap;
    //             cv::resize(depthAugmented, depthMap, cv::Size{}, stride, stride, cv::INTER_LINEAR);
    //             char imagename [100];
    //             sprintf(imagename, "visualize/%s_augment_%04d_label_part_depth.png", mModelString.c_str(),
    //                     metaData.writeNumber);
    //             cv::imwrite(imagename, depthMap);
    //         }
    //     }
    // }
}

template<typename Dtype>
void OPDataTransformer<Dtype>::generateDepthLabelMap(Dtype* transformedLabel, const cv::Mat& depth) const
{
    const auto gridX = (int)depth.cols;
    const auto gridY = (int)depth.rows;
    const auto channelOffset = gridY * gridX;
    const auto numberBpPafChannels = getNumberBodyAndPafChannels(mPoseModel);
    // generate depth
    for (auto gY = 0; gY < gridY; gY++)
    {
        const auto yOffset = gY*gridX;
        for (auto gX = 0; gX < gridX; gX++)
        {
            const auto xyOffset = yOffset + gX;
            auto depth_val = depth.at<uint16_t>(gY, gX);
            transformedLabel[(2*numberBpPafChannels+2)*channelOffset + xyOffset] = (depth_val>0)?1.0:0.0;
            transformedLabel[(2*numberBpPafChannels+3)*channelOffset + xyOffset] = float(depth_val)/1000.0;
        }
    }
}

float getNorm(const cv::Point2f& pointA, const cv::Point2f& pointB)
{
    const auto difference = pointA - pointB;
    return std::sqrt(difference.x*difference.x + difference.y*difference.y);
}

void maskHands(cv::Mat& maskMiss, const std::vector<float>& isVisible, const std::vector<cv::Point2f>& points,
              const float stride, const float ratio)
{
    for (auto part = 0 ; part < 2 ; part++)
    {
        const auto shoulderIndex = (part == 0 ? 5:2);
        const auto elbowIndex = shoulderIndex+1;
        const auto wristIndex = elbowIndex+1;
        if (isVisible.at(shoulderIndex) != 2 && isVisible.at(elbowIndex) != 2 && isVisible.at(wristIndex) != 2)
        {
            const auto ratioStride = 1.f / stride;
            const auto wrist = ratioStride * points.at(wristIndex);
            const auto elbow = ratioStride * points.at(elbowIndex);
            const auto shoulder = ratioStride * points.at(shoulderIndex);

            const auto distance = (int)std::round(ratio*std::max(getNorm(wrist, elbow), getNorm(elbow, shoulder)));
            const cv::Point momentum = (wrist-elbow)*0.25f;
            cv::Rect roi{(int)std::round(wrist.x + momentum.x - distance /*- wrist.x/2.f*/),
                         (int)std::round(wrist.y + momentum.y - distance /*- wrist.y/2.f*/),
                         2*distance, 2*distance};
            // Apply ROI
            keepRoiInside(roi, maskMiss.size());
            if (roi.area() > 0)
                maskMiss(roi).setTo(0.f); // For debugging use 0.5f
        }
        // // If there is no visible desired keypoints, mask out the whole background
        // else
        //     maskMiss.setTo(0.f); // For debugging use 0.5f
    }
}

void maskFeet(cv::Mat& maskMiss, const std::vector<float>& isVisible, const std::vector<cv::Point2f>& points,
              const float stride, const float ratio)
{
    for (auto part = 0 ; part < 2 ; part++)
    {
        const auto kneeIndex = 9+part*5;
        const auto ankleIndex = kneeIndex+1;
        if (isVisible.at(kneeIndex) != 2 && isVisible.at(ankleIndex) != 2)
        {
            const auto ratioStride = 1.f / stride;
            const auto knee = ratioStride * points.at(kneeIndex);
            const auto ankle = ratioStride * points.at(ankleIndex);
            const auto distance = (int)std::round(ratio*getNorm(knee, ankle));
            const cv::Point momentum = (ankle-knee)*0.15f;
            cv::Rect roi{(int)std::round(ankle.x + momentum.x)-distance,
                         (int)std::round(ankle.y + momentum.y)-distance,
                         2*distance, 2*distance};
            // Apply ROI
            keepRoiInside(roi, maskMiss.size());
            if (roi.area() > 0)
                maskMiss(roi).setTo(0.f); // For debugging use 0.5f
        }
        // // If there is no visible desired keypoints, mask out the whole background
        // else
        //     maskMiss.setTo(0.f); // For debugging use 0.5f
    }
}

template<typename Dtype>
void fillMaskChannels(Dtype* transformedLabel, const int gridX, const int gridY, const int numberTotalChannels,
                      const int channelOffset, const cv::Mat& maskMiss)
{
    // Initialize labels to [0, 1] (depending on maskMiss)
    // // Naive version (very slow)
    // for (auto gY = 0; gY < gridY; gY++)
    // {
    //     const auto yOffset = gY*gridX;
    //     for (auto gX = 0; gX < gridX; gX++)
    //     {
    //         const auto xyOffset = yOffset + gX;
    //         const float weight = float(maskMiss.at<uchar>(gY, gX)) / 255.f;
    //         // Body part & PAFs & background channel & distance
    //         for (auto part = 0; part < numberTotalChannels; part++)
    //         // // For Distance
    //         // for (auto part = 0; part < numberTotalChannels - numberPafChannels/2; part++)
    //             transformedLabel[part*channelOffset + xyOffset] = weight;
    //     }
    // }
    // OpenCV wrapper: ~10x speed up with baseline
    cv::Mat maskMissFloat;
    const auto type = getType(Dtype(0));
    maskMiss.convertTo(maskMissFloat, type);
    maskMissFloat /= Dtype(255.f);
    // // For Distance
    // for (auto part = 0; part < numberTotalChannels - numberPafChannels/2; part++)
    for (auto part = 0; part < numberTotalChannels; part++)
    {
        auto* pointer = &transformedLabel[part*channelOffset];
        cv::Mat transformedLabel(gridY, gridX, type, (unsigned char*)(pointer));
        // // Not exactly 0 for limited floating precission
        // CHECK_LT(std::abs(cv::norm(transformedLabel-maskMissFloat)), 1e-6);
        maskMissFloat.copyTo(transformedLabel);
    }
}

template<typename Dtype>
void OPDataTransformer<Dtype>::generateLabelMap(Dtype* transformedLabel, const cv::Size& imageSize, const cv::Mat& maskMiss,
                                                const MetaData& metaData) const
{
    // Label size = image size / stride
    const auto rezX = (int)imageSize.width;
    const auto rezY = (int)imageSize.height;
    const auto stride = (int)param_.stride();
    const auto gridX = rezX / stride;
    const auto gridY = rezY / stride;
    const auto channelOffset = gridY * gridX;
    const auto numberBodyParts = getNumberBodyParts(mPoseModel); // #BP
    const auto numberPafChannels = getNumberPafChannels(mPoseModel); // 2 x #PAF
    const auto numberTotalChannels = getNumberBodyBkgAndPAF(mPoseModel); // numberBodyParts + numberPafChannels + 1
    // // For Distance
    // const auto numberTotalChannels = getNumberBodyBkgAndPAF(mPoseModel) + (numberPafChannels / 2); // numberBodyParts + numberPafChannels + 1

    // Labels to 0
    std::fill(transformedLabel, transformedLabel + 2*numberTotalChannels * gridY * gridX, 0.f);

    // Initialize labels to [0, 1] (depending on maskMiss)
    fillMaskChannels(transformedLabel, gridX, gridY, numberTotalChannels, channelOffset, maskMiss);

    // Masking out channels - For COCO_YY_ZZ models (ZZ < YY)
    if (numberBodyParts > getNumberBodyPartsLmdb(mPoseModel) || mPoseModel == PoseModel::MPII_59)
    {
        // Remove BP/PAF non-labeled channels
        const auto missingChannels = getMissingChannels(mPoseModel, (mPoseModel == PoseModel::MPII_59
                                                                        ? metaData.jointsSelf.isVisible
                                                                        : std::vector<float>{}));
        for (const auto& index : missingChannels)
            std::fill(&transformedLabel[index*channelOffset],
                      &transformedLabel[index*channelOffset + channelOffset], 0);
        // Background
        const auto type = getType(Dtype(0));
        const auto backgroundIndex = numberPafChannels + numberBodyParts;
        cv::Mat maskMissTemp(gridY, gridX, type, &transformedLabel[backgroundIndex*channelOffset]);
        // If hands
        if (numberBodyParts == 59 && mPoseModel != PoseModel::MPII_59)
        {
            maskHands(maskMissTemp, metaData.jointsSelf.isVisible, metaData.jointsSelf.points, stride, 0.6f);
            for (const auto& jointsOther : metaData.jointsOthers)
                maskHands(maskMissTemp, jointsOther.isVisible, jointsOther.points, stride, 0.6f);
        }
        // If foot
        if (numberBodyParts == 23)
        {
            maskFeet(maskMissTemp, metaData.jointsSelf.isVisible, metaData.jointsSelf.points, stride, 0.6f);
            for (const auto& jointsOther : metaData.jointsOthers)
                maskFeet(maskMissTemp, jointsOther.isVisible, jointsOther.points, stride, 0.6f);
        }
    }

// TODO: Remove, temporary hack to get foot data, do nicely for 6-keypoint foot
    // Remove if required RBigToe, RSmallToe, LBigToe, LSmallToe, and Background
    if (mPoseModel == PoseModel::COCO_23 || mPoseModel == PoseModel::DOME_23_19 || mPoseModel == PoseModel::COCO_23_17)
    {
        std::vector<int> indexesToRemove;
        // PAFs
        for (const auto& index : {11, 12, 15, 16})
        {
            const auto indexBase = 2*index;
            indexesToRemove.emplace_back(indexBase);
            indexesToRemove.emplace_back(indexBase+1);
        }
        // Body parts
        for (const auto& index : {11, 12, 16, 17})
        {
            const auto indexBase = numberPafChannels + index;
            indexesToRemove.emplace_back(indexBase);
        }
        // Included in code 10-30 lines above...
        // // Dome data: Exclude (unlabeled) foot keypoints
        // if (mPoseModel == PoseModel::DOME_23_19 || mPoseModel == PoseModel::COCO_23_17)
        // {
        //     // Remove those channels
        //     for (const auto& index : indexesToRemove)
        //     {
        //         std::fill(&transformedLabel[index*channelOffset],
        //                   &transformedLabel[index*channelOffset + channelOffset], 0);
        //     }
        // }
        // // Background
        // if (mPoseModel == PoseModel::DOME_23_19 || mPoseModel == PoseModel::COCO_23_17)
        // {
        //     const auto backgroundIndex = numberPafChannels + numberBodyParts;
        //     int type;
        //     const auto type = getType(Dtype(0));
        //     cv::Mat maskMiss(gridY, gridX, type, &transformedLabel[backgroundIndex*channelOffset]);
        //     maskFeet(maskMiss, metaData.jointsSelf.isVisible, metaData.jointsSelf.points, stride, 0.6f);
        //     for (const auto& jointsOther : metaData.jointsOthers)
        //         maskFeet(maskMiss, jointsOther.isVisible, jointsOther.points, stride, 0.6f);
        // }
        // Mask foot region over person whose feet are not anotated with a square
        if (mPoseModel == PoseModel::COCO_23)
        {
            // auto visualize = false;
            // // From current annotation
            // const auto& selfPoints = metaData.jointsSelf.points;
            // const auto& selfVisible = metaData.jointsSelf.isVisible;
            // if (selfVisible.at(11) == 2.f && selfVisible.at(12) == 2.f
            //     && selfVisible.at(16) == 2.f && selfVisible.at(17) == 2.f)
            // {
            //     // If knees and ankles visible
            //     if (selfVisible.at(9) != 2 && selfVisible.at(10) != 2
            //         && selfVisible.at(14) != 2 && selfVisible.at(15) != 2)
            //     {
            //         maskFeet(maskMiss, selfVisible, selfPoints, 0.75f);
            //     }
            // }
            // From side annotations
            for (const auto& jointsOther : metaData.jointsOthers)
            {
                const auto& otherPoints = jointsOther.points;
                const auto& otherVisible = jointsOther.isVisible;
                // If no points visible
                if (otherVisible.at(11) == 2.f && otherVisible.at(12) == 2.f
                    && otherVisible.at(16) == 2.f && otherVisible.at(17) == 2.f)
                {
                    // If knees and ankles visible
                    if (otherVisible.at(9) != 2 && otherVisible.at(10) != 2
                        && otherVisible.at(14) != 2 && otherVisible.at(15) != 2)
                    {
                        for (auto index : indexesToRemove)
                        {
                            const auto type = getType(Dtype(0));
                            cv::Mat maskMiss(gridY, gridX, type, &transformedLabel[index*channelOffset]);
                            maskFeet(maskMiss, otherVisible, otherPoints, stride, 0.6f);
                        }
                        // visualize = true;
                    }
                }
            }
            // if (visualize)
            // {
            //     // Visualizing
            //     for (auto part = 0; part < 2*numberTotalChannels; part++)
            //     {
            //         // Reduce #images saved (ideally images from 0 to numberTotalChannels should be the same)
            //         // if (part >= 11*2)
            //         if (part >= 22 && part <= numberTotalChannels)
            //         // if (part < 3 || part >= numberTotalChannels - 3)
            //         {
            //             cv::Mat labelMap = cv::Mat::zeros(gridY, gridX, CV_8UC1);
            //             for (auto gY = 0; gY < gridY; gY++)
            //             {
            //                 const auto yOffset = gY*gridX;
            //                 for (auto gX = 0; gX < gridX; gX++)
            //                     labelMap.at<uchar>(gY,gX) = (int)(transformedLabel[part*channelOffset + yOffset + gX]*255);
            //             }
            //             cv::resize(labelMap, labelMap, cv::Size{}, stride, stride, cv::INTER_LINEAR);
            //             cv::applyColorMap(labelMap, labelMap, cv::COLORMAP_JET);
            //             cv::addWeighted(labelMap, 0.5, image, 0.5, 0.0, labelMap);
            //             // Write on disk
            //             char imagename [100];
            //             sprintf(imagename, "visualize/augment_%04d_label_part_%02d.jpg", metaData.writeNumber, part);
            //             cv::imwrite(imagename, labelMap);
            //         }
            //     }
            // }
        }
    }

    // PAFs
    const auto& labelMapA = getPafIndexA(mPoseModel);
    const auto& labelMapB = getPafIndexB(mPoseModel);
    const auto threshold = 1;
    const auto diagonal = sqrt(gridX*gridX + gridY*gridY);
    const auto diagonalProportion = (mCurrentEpoch > 0 ? 1.f : metaData.writeNumber/(float)metaData.totalWriteNumber);
    for (auto i = 0 ; i < labelMapA.size() ; i++)
    {
        cv::Mat count = cv::Mat::zeros(gridY, gridX, CV_8UC1);
        // Self
        const auto& joints = metaData.jointsSelf;
        if (joints.isVisible[labelMapA[i]] <= 1 && joints.isVisible[labelMapB[i]] <= 1)
        {
            putVectorMaps(transformedLabel + (numberTotalChannels + 2*i)*channelOffset,
                          transformedLabel + (numberTotalChannels + 2*i + 1)*channelOffset,
                          transformedLabel + 2*i*channelOffset,
                          transformedLabel + (2*i + 1)*channelOffset,
                          // // For Distance
                          // transformedLabel + (2*numberTotalChannels - numberPafChannels/2 + i)*channelOffset,
                          // transformedLabel + (numberTotalChannels - numberPafChannels/2 + i)*channelOffset,
                          count, joints.points[labelMapA[i]], joints.points[labelMapB[i]],
                          param_.stride(), gridX, gridY, param_.sigma(), threshold,
                          diagonal, diagonalProportion);
        }

        // For every other person
        for (auto otherPerson = 0; otherPerson < metaData.numberOtherPeople; otherPerson++)
        {
            const auto& joints = metaData.jointsOthers[otherPerson];
            if (joints.isVisible[labelMapA[i]] <= 1 && joints.isVisible[labelMapB[i]] <= 1)
            {
                putVectorMaps(transformedLabel + (numberTotalChannels + 2*i)*channelOffset,
                              transformedLabel + (numberTotalChannels + 2*i + 1)*channelOffset,
                              transformedLabel + 2*i*channelOffset,
                              transformedLabel + (2*i + 1)*channelOffset,
                              // // For Distance
                              // transformedLabel + (2*numberTotalChannels - numberPafChannels/2 + i)*channelOffset,
                              // transformedLabel + (numberTotalChannels - numberPafChannels/2 + i)*channelOffset,
                              count, joints.points[labelMapA[i]], joints.points[labelMapB[i]],
                              param_.stride(), gridX, gridY, param_.sigma(), threshold,
                              diagonal, diagonalProportion);
            }
        }
    }
    // // Re-normalize masks (otherwise PAF explodes)
    // const auto finalImageArea = gridX*gridY;
    // for (auto i = 0 ; i < labelMapA.size() ; i++)
    // {
    //     auto* initPoint = &transformedLabel[2*i*channelOffset];
    //     const auto accumulation = std::accumulate(initPoint, initPoint+channelOffset, 0);
    //     const auto ratio = finalImageArea / (float)accumulation;
    //     if (ratio > 1.01 || ratio < 0.99)
    //         std::transform(initPoint, initPoint + 2*channelOffset, initPoint, std::bind1st(std::multiplies<Dtype>(), ratio)) ;
    // }

    // Body parts
    for (auto part = 0; part < numberBodyParts; part++)
    {
        // Self
        if (metaData.jointsSelf.isVisible[part] <= 1)
        {
            const auto& centerPoint = metaData.jointsSelf.points[part];
            putGaussianMaps(transformedLabel + (part+numberTotalChannels+numberPafChannels)*channelOffset,
                            centerPoint, param_.stride(), gridX, gridY, param_.sigma());
        }
        // For every other person
        for (auto otherPerson = 0; otherPerson < metaData.numberOtherPeople; otherPerson++)
        {
            if (metaData.jointsOthers[otherPerson].isVisible[part] <= 1)
            {
                const auto& centerPoint = metaData.jointsOthers[otherPerson].points[part];
                putGaussianMaps(transformedLabel + (part+numberTotalChannels+numberPafChannels)*channelOffset,
                                centerPoint, param_.stride(), gridX, gridY, param_.sigma());
            }
        }
    }

    // Background channel
    // Naive implementation
    for (auto gY = 0; gY < gridY; gY++)
    {
        const auto yOffset = gY*gridX;
        for (auto gX = 0; gX < gridX; gX++)
        {
            const auto xyOffset = yOffset + gX;
            Dtype maximum = 0.;
            const auto backgroundIndex = numberTotalChannels+numberPafChannels+numberBodyParts;
            for (auto part = numberTotalChannels+numberPafChannels ; part < backgroundIndex ; part++)
            {
                const auto index = part * channelOffset + xyOffset;
                maximum = (maximum > transformedLabel[index]) ? maximum : transformedLabel[index];
            }
            transformedLabel[backgroundIndex*channelOffset + xyOffset] = std::max(Dtype(1.)-maximum, Dtype(0.));
        }
    }
}

template<typename Dtype>
void OPDataTransformer<Dtype>::putGaussianMaps(Dtype* entry, const cv::Point2f& centerPoint, const int stride,
                                               const int gridX, const int gridY, const float sigma) const
{
    //LOG(INFO) << "putGaussianMaps here we start for " << centerPoint.x << " " << centerPoint.y;
    const Dtype start = stride/2.f - 0.5f; //0 if stride = 1, 0.5 if stride = 2, 1.5 if stride = 4, ...
    const auto multiplier = 2.0 * sigma * sigma;
    for (auto gY = 0; gY < gridY; gY++)
    {
        const auto yOffset = gY*gridX;
        const Dtype y = start + gY * stride;
        const auto yMenosCenterPointSquared = (y-centerPoint.y)*(y-centerPoint.y);
        for (auto gX = 0; gX < gridX; gX++)
        {
            const Dtype x = start + gX * stride;
            const Dtype d2 = (x-centerPoint.x)*(x-centerPoint.x) + yMenosCenterPointSquared;
            const Dtype exponent = d2 / multiplier;
            //ln(100) = -ln(1%)
            if (exponent <= 4.6052)
            {
                const auto xyOffset = yOffset + gX;
                // Option a) Max
                entry[xyOffset] = std::min(Dtype(1), std::max(entry[xyOffset], std::exp(-exponent)));
                // // Option b) Average
                // entry[xyOffset] += std::exp(-exponent);
                // if (entry[xyOffset] > 1)
                //     entry[xyOffset] = 1;
            }
        }
    }
}

template<typename Dtype>
void OPDataTransformer<Dtype>::putVectorMaps(Dtype* entryX, Dtype* entryY, Dtype* maskX, Dtype* maskY,
                                             cv::Mat& count, const cv::Point2f& centerA,
                                             const cv::Point2f& centerB, const int stride, const int gridX,
                                             const int gridY, const float sigma, const int threshold,
                                             const int diagonal, const float diagonalProportion) const
// void OPDataTransformer<Dtype>::putVectorMaps(Dtype* entryX, Dtype* entryY, Dtype* entryD, Dtype* entryDMask,
//                                              cv::Mat& count, const cv::Point2f& centerA,
//                                              const cv::Point2f& centerB, const int stride, const int gridX,
//                                              const int gridY, const float sigma, const int threshold) const
{
    const auto scaleLabel = Dtype(1)/Dtype(stride);
    const auto centerALabelScale = scaleLabel * centerA;
    const auto centerBLabelScale = scaleLabel * centerB;
    cv::Point2f directionAB = centerBLabelScale - centerALabelScale;
    const auto distanceAB = std::sqrt(directionAB.x*directionAB.x + directionAB.y*directionAB.y);
    directionAB *= (Dtype(1) / distanceAB);

    // // For Distance
    // const auto dMin = Dtype(0);
    // const auto dMax = Dtype(std::sqrt(gridX*gridX + gridY*gridY));
    // const auto dRange = dMax - dMin;
    // const auto entryDValue = 2*(distanceAB - dMin)/dRange - 1; // Main range: [-1, 1], -1 is 0px-distance, 1 is 368 / stride x sqrt(2) px of distance

    // If PAF is not 0 or NaN (e.g. if PAF perpendicular to image plane)
    if (!isnan(directionAB.x) && !isnan(directionAB.y))
    {
        const int minX = std::max(0,
                                  int(std::round(std::min(centerALabelScale.x, centerBLabelScale.x) - threshold)));
        const int maxX = std::min(gridX,
                                  int(std::round(std::max(centerALabelScale.x, centerBLabelScale.x) + threshold)));
        const int minY = std::max(0,
                                  int(std::round(std::min(centerALabelScale.y, centerBLabelScale.y) - threshold)));
        const int maxY = std::min(gridY,
                                  int(std::round(std::max(centerALabelScale.y, centerBLabelScale.y) + threshold)));
(void)diagonalProportion;
(void)diagonal;
(void)entryX;
(void)entryY;
        // const auto weight = (1-diagonalProportion) + diagonalProportion * diagonal/distanceAB; // alpha*1 + (1-alpha)*realProportion
        for (auto gY = minY; gY < maxY; gY++)
        {
            const auto yOffset = gY*gridX;
            const auto gYMenosCenterALabelScale = gY - centerALabelScale.y;
            for (auto gX = minX; gX < maxX; gX++)
            {
                const auto xyOffset = yOffset + gX;
                const cv::Point2f ba{gX - centerALabelScale.x, gYMenosCenterALabelScale};
                const float distance = std::abs(ba.x*directionAB.y - ba.y*directionAB.x);
                if (distance <= threshold)
                {
                    auto& counter = count.at<uchar>(gY, gX);
                    if (counter == 0)
                    {
                        entryX[xyOffset] = directionAB.x;
                        entryY[xyOffset] = directionAB.y;
                        // Weight makes small PAFs as important as big PAFs
                        // maskX[xyOffset] *= weight;
                        // maskY[xyOffset] *= weight;
                        // // For Distance
                        // entryD[xyOffset] = entryDValue;
                        // entryDMask[xyOffset] = Dtype(1);
                    }
                    else
                    {
                        entryX[xyOffset] = (entryX[xyOffset]*counter + directionAB.x) / (counter + 1);
                        entryY[xyOffset] = (entryY[xyOffset]*counter + directionAB.y) / (counter + 1);
                        // // For Distance
                        // entryD[xyOffset] = (entryD[xyOffset]*counter + entryDValue) / (counter + 1);
                    }
                    counter++;
                }
            }
        }
    }
}
// OpenPose: added end

INSTANTIATE_CLASS(OPDataTransformer);

}  // namespace caffe
