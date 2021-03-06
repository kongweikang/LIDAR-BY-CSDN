/*
 * Software License Agreement (BSD License)
 *
 *  Point Cloud Library (PCL) - www.pointclouds.org
 *  Copyright (c) 2010, Willow Garage, Inc.
 *  Copyright (c) 2012-, Open Perception, Inc.
 *
 *  All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions
 *  are met:
 *
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above
 *     copyright notice, this list of conditions and the following
 *     disclaimer in the documentation and/or other materials provided
 *     with the distribution.
 *   * Neither the name of the copyright holder(s) nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 *  FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 *  COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 *  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 *  BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 *  LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 *  CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 *  LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 *  ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 *  POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include <vtkVersion.h>
#if VTK_MAJOR_VERSION == 9 && VTK_MINOR_VERSION == 0
#include <limits> // This must be included before vtkPolyData.h
#endif
#include <vtkPolyData.h>
#include <vtkCleanPolyData.h>
#include <vtkSmartPointer.h>

#include <pcl/visualization/common/io.h>
#include <pcl/kdtree/kdtree_flann.h>
#include <pcl/io/pcd_io.h>
#include <pcl/memory.h>

//////////////////////////////////////////////////////////////////////////////////////////////
void
pcl::visualization::getCorrespondingPointCloud (vtkPolyData *src, 
                                                const pcl::PointCloud<pcl::PointXYZ> &tgt, 
                                                pcl::Indices &indices)
{
  // Iterate through the points and copy the data in a pcl::PointCloud
  pcl::PointCloud<pcl::PointXYZ> cloud;
  cloud.height = 1; cloud.width = static_cast<std::uint32_t> (src->GetNumberOfPoints ());
  cloud.is_dense = false;
  cloud.resize (cloud.width * cloud.height);
  for (vtkIdType i = 0; i < src->GetNumberOfPoints (); i++)
  {
    double p[3];
    src->GetPoint (i, p);
    cloud[i].x = static_cast<float> (p[0]); 
    cloud[i].y = static_cast<float> (p[1]); 
    cloud[i].z = static_cast<float> (p[2]);
  }

  // Compute a kd-tree for tgt
  pcl::KdTreeFLANN<pcl::PointXYZ> kdtree;
  kdtree.setInputCloud (make_shared<PointCloud<PointXYZ>> (tgt));

  pcl::Indices nn_indices (1);
  std::vector<float> nn_dists (1);
  // For each point on screen, find its correspondent in the target
  for (const auto &point : cloud.points)
  {
    kdtree.nearestKSearch (point, 1, nn_indices, nn_dists);
    indices.push_back (nn_indices[0]);
  }
  // Sort and remove duplicate indices
  std::sort (indices.begin (), indices.end ());
  indices.erase (std::unique (indices.begin (), indices.end ()), indices.end ()); 
}

//////////////////////////////////////////////////////////////////////////////////////////////
bool 
pcl::visualization::savePointData (vtkPolyData* data, const std::string &out_file, const CloudActorMapPtr &actors)
{
  // Clean the data (no duplicates!)
  vtkSmartPointer<vtkCleanPolyData> cleaner = vtkSmartPointer<vtkCleanPolyData>::New ();
  cleaner->SetTolerance (0.0);
  cleaner->SetInputData (data);
  cleaner->ConvertLinesToPointsOff ();
  cleaner->ConvertPolysToLinesOff ();
  cleaner->ConvertStripsToPolysOff ();
  cleaner->PointMergingOn ();
  cleaner->Update ();

  // If we pruned any points, print the number of points pruned to screen
  if (cleaner->GetOutput ()->GetNumberOfPoints () != data->GetNumberOfPoints ())
  {
    int nr_pts_pruned = static_cast<int> (data->GetNumberOfPoints () - cleaner->GetOutput ()->GetNumberOfPoints ());
    pcl::console::print_highlight ("Number of points pruned: "); pcl::console::print_value ("%d\n", nr_pts_pruned);
  }

  // Attempting to load all Point Cloud data input files (using the actor name)...
  int i = 1;
  for (const auto &actor : *actors)
  {
    std::string file_name = actor.first;

    // Is there a ".pcd" in the name? If no, then do not attempt to load this actor
    std::string::size_type position;
    if ((position = file_name.find (".pcd")) == std::string::npos)
      continue;

    // Strip the ".pcd-X"
    file_name = file_name.substr (0, position) + ".pcd";

    pcl::console::print_debug ("  Load: %s ... ", file_name.c_str ());
    // Assume the name of the actor is the name of the file
    pcl::PCLPointCloud2 cloud;
    if (pcl::io::loadPCDFile (file_name, cloud) == -1)
    {
      pcl::console::print_error (stdout, "[failed]\n");
      return (false);
    }
    pcl::console::print_debug ("[success]\n");
 
    pcl::PointCloud<pcl::PointXYZ> cloud_xyz;
    pcl::fromPCLPointCloud2 (cloud, cloud_xyz);
    // Get the corresponding indices that we need to save from this point cloud
    pcl::Indices indices;
    getCorrespondingPointCloud (cleaner->GetOutput (), cloud_xyz, indices);

    // Copy the indices and save the file
    pcl::PCLPointCloud2 cloud_out;
    pcl::copyPointCloud (cloud, indices, cloud_out);
    const std::string out_filename = out_file + std::to_string(i++) + ".pcd";
    pcl::console::print_debug ("  Save: %s ... ", out_filename.c_str ());
    if (pcl::io::savePCDFile (out_filename, cloud_out, Eigen::Vector4f::Zero (),
                              Eigen::Quaternionf::Identity (), true) == -1)
    {
      pcl::console::print_error (stdout, "[failed]\n");
      return (false);
    }
    pcl::console::print_debug ("[success]\n");
  }

  return (true);
}
