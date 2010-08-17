/* +---------------------------------------------------------------------------+
   |          The Mobile Robot Programming Toolkit (MRPT) C++ library          |
   |                                                                           |
   |                   http://mrpt.sourceforge.net/                            |
   |                                                                           |
   |   Copyright (C) 2005-2010  University of Malaga                           |
   |                                                                           |
   |    This software was written by the Machine Perception and Intelligent    |
   |      Robotics Lab, University of Malaga (Spain).                          |
   |    Contact: Jose-Luis Blanco  <jlblanco@ctima.uma.es>                     |
   |                                                                           |
   |  This file is part of the MRPT project.                                   |
   |                                                                           |
   |     MRPT is free software: you can redistribute it and/or modify          |
   |     it under the terms of the GNU General Public License as published by  |
   |     the Free Software Foundation, either version 3 of the License, or     |
   |     (at your option) any later version.                                   |
   |                                                                           |
   |   MRPT is distributed in the hope that it will be useful,                 |
   |     but WITHOUT ANY WARRANTY; without even the implied warranty of        |
   |     MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         |
   |     GNU General Public License for more details.                          |
   |                                                                           |
   |     You should have received a copy of the GNU General Public License     |
   |     along with MRPT.  If not, see <http://www.gnu.org/licenses/>.         |
   |                                                                           |
   +---------------------------------------------------------------------------+ */

#include <mrpt/base.h>  // Precompiled headers

#include <mrpt/poses/CNetworkOfPoses.h>

using namespace mrpt;
using namespace mrpt::utils;
using namespace mrpt::poses;
using namespace mrpt::system;


namespace mrpt
{
	namespace poses
	{
		namespace detail
		{
			template<class CPOSE>
			void save_graph_of_poses_from_text_file(const CNetworkOfPoses<CPOSE> *g, const std::string &fil)
			{
				THROW_EXCEPTION("TO DO");

			} // end save_graph

			template<class CPOSE>
			void load_graph_of_poses_from_text_file(CNetworkOfPoses<CPOSE>*g, const std::string &fil)
			{
				set<string>  alreadyWarnedUnknowns; // for unknown line types, show a warning to cerr just once.

				// First, empty the graph:
				g->clear();

				std::ifstream  f;
				f.open(fil.c_str());
				if (!f.is_open())
					THROW_EXCEPTION_CUSTOM_MSG1("Error opening file '%s' for reading",fil.c_str());

				// -------------------------------------------
				// 1st PASS: Read EQUIV entries only
				//  since processing them AFTER loading the data
				//  is much much slower.
				// -------------------------------------------
				map<TNodeID,TNodeID> lstEquivs; // List of EQUIV entries: NODEID -> NEWNODEID. NEWNODEID will be always the lowest ID number.

				// Read & process lines each at once until EOF:
				for (unsigned int lineNum = 1; !f.fail();  ++lineNum )
				{
					string lin;
					getline(f,lin);

					lin = mrpt::system::trim(lin);
					if (lin.empty()) continue; // Ignore empty lines.

					// Ignore comments lines, starting with "#" or "//".
					if ( mrpt::system::strStarts(lin,"#") || mrpt::system::strStarts(lin,"//") )
						continue;

					// Parse the line as a string stream:
					istringstream s;
					s.str(lin);

					string key;
					if ( !(s >> key) || key.empty() )
						THROW_EXCEPTION(format("Line %u: Can't read string for entry type in: '%s'", lineNum, lin.c_str() ) );

					if ( strCmpI(key,"EQUIV") )
					{
						// Process these ones at the end, for now store in a list:
						TNodeID  id1,id2;
						if (!(s>> id1 >> id2))
							THROW_EXCEPTION(format("Line %u: Can't read id1 & id2 in EQUIV line: '%s'", lineNum, lin.c_str() ) );
						lstEquivs[std::max(id1,id2)] = std::min(id1,id2);
					}
				} // end 1st pass

				// -------------------------------------------
				// 2nd PASS: Read all other entries
				// -------------------------------------------
				f.clear();
				f.seekg(0);

				// Read & process lines each at once until EOF:
				for (unsigned int lineNum = 1; !f.fail();  ++lineNum )
				{
					string lin;
					getline(f,lin);

					lin = mrpt::system::trim(lin);
					if (lin.empty()) continue; // Ignore empty lines.

					// Ignore comments lines, starting with "#" or "//".
					if ( mrpt::system::strStarts(lin,"#") || mrpt::system::strStarts(lin,"//") )
						continue;

					// Parse the line as a string stream:
					istringstream s;
					s.str(lin);

					// Recognized strings:
					//  VERTEX2 id x y phi
					//  EDGE2 to_id from_id Ax Ay Aphi inf_xx inf_xy inf_yy inf_pp inf_xp inf_yp
					//  VERTEX3 x y z roll pitch yaw
					//  EDGE3 to_id from_id Ax Ay Az Aroll Apitch Ayaw inf_11 inf_12 .. inf_16 inf_22 .. inf_66
					//  EQUIV id1 id2
					string key;
					if ( !(s >> key) || key.empty() )
						THROW_EXCEPTION(format("Line %u: Can't read string for entry type in: '%s'", lineNum, lin.c_str() ) );

					if ( strCmpI(key,"VERTEX2") )
					{
						TNodeID  id;
						TPose2D  p2D;
						if (!(s>> id >> p2D.x >> p2D.y >> p2D.phi))
							THROW_EXCEPTION(format("Line %u: Error parsing VERTEX2 line: '%s'", lineNum, lin.c_str() ) );

						// Make sure the node is new:
						if (g->nodes.find(id)!=g->nodes.end())
							THROW_EXCEPTION(format("Line %u: Error, duplicated verted ID %u in line: '%s'", lineNum, static_cast<unsigned int>(id), lin.c_str() ) );

						// EQUIV? Replace ID by new one.
						{
							const map<TNodeID,TNodeID>::const_iterator itEq = lstEquivs.find(id);
							if (itEq!=lstEquivs.end()) id = itEq->second;
						}

						// Add to map: ID -> absolute pose:
						if (g->nodes.find(id)==g->nodes.end())
						{
							typename CNetworkOfPoses<CPOSE>::contraint_t::type_value & newNode = g->nodes[id];
							newNode = CPose2D(p2D); // Auto converted to CPose3D if needed
						}
					}
					else if ( strCmpI(key,"EDGE2") )
					{
						//  EDGE2 to_id from_id Ax Ay Aphi inf_xx inf_xy inf_yy inf_pp inf_xp inf_yp
						//                                   s00   s01     s11    s22    s02    s12
						//  Read values are:
						//    [ s00 s01 s02 ]
						//    [  -  s11 s12 ]
						//    [  -   -  s22 ]
						//
						TNodeID  to_id, from_id;
						if (!(s>> to_id >> from_id))
							THROW_EXCEPTION(format("Line %u: Error parsing EDGE2 line: '%s'", lineNum, lin.c_str() ) );

						// EQUIV? Replace ID by new one.
						{
							const map<TNodeID,TNodeID>::const_iterator itEq = lstEquivs.find(to_id);
							if (itEq!=lstEquivs.end()) to_id = itEq->second;
						}
						{
							const map<TNodeID,TNodeID>::const_iterator itEq = lstEquivs.find(from_id);
							if (itEq!=lstEquivs.end()) from_id = itEq->second;
						}

						TPose2D  Ap_mean;
						CMatrixDouble33 Ap_cov_inv;
						if (!(s>>
								Ap_mean.x >> Ap_mean.y >> Ap_mean.phi >>
								Ap_cov_inv(0,0) >> Ap_cov_inv(0,1) >> Ap_cov_inv(1,1) >>
								Ap_cov_inv(2,2) >> Ap_cov_inv(0,2) >> Ap_cov_inv(1,2) ))
							THROW_EXCEPTION(format("Line %u: Error parsing EDGE2 line: '%s'", lineNum, lin.c_str() ) );

						// Complete low triangular part of inf matrix:
						Ap_cov_inv(1,0) = Ap_cov_inv(0,1);
						Ap_cov_inv(2,0) = Ap_cov_inv(0,2);
						Ap_cov_inv(2,1) = Ap_cov_inv(1,2);

						// Convert to 2D cov, 3D cov or 3D inv_cov as needed:
						typename CNetworkOfPoses<CPOSE>::edge_t  newEdge;
						newEdge.copyFrom( CPosePDFGaussianInf( CPose2D(Ap_mean), Ap_cov_inv ) );
						g->insertEdge(from_id, to_id, newEdge);
					}
					else if ( strCmpI(key,"EQUIV") )
					{
						// Already read in the 1st pass.
					}
					else
					{	// Unknown entry: Warn the user just once:
						if (alreadyWarnedUnknowns.find(key)==alreadyWarnedUnknowns.end())
						{
							alreadyWarnedUnknowns.insert(key);
							cerr << "[CNetworkOfPoses::loadFromTextFile] " << fil << ":" << lineNum << ": Warning: unknown entry type: " << key << endl;
						}
					}
				} // end while

			} // end load_graph

		}
	}
}

// Explicit instantations:
template void BASE_IMPEXP mrpt::poses::detail::save_graph_of_poses_from_text_file<CPosePDFGaussian>(const CNetworkOfPoses<CPosePDFGaussian> *g, const std::string &fil);
template void BASE_IMPEXP mrpt::poses::detail::save_graph_of_poses_from_text_file<CPose3DPDFGaussian>(const CNetworkOfPoses<CPose3DPDFGaussian> *g, const std::string &fil);
template void BASE_IMPEXP mrpt::poses::detail::save_graph_of_poses_from_text_file<CPosePDFGaussianInf>(const CNetworkOfPoses<CPosePDFGaussianInf> *g, const std::string &fil);
template void BASE_IMPEXP mrpt::poses::detail::save_graph_of_poses_from_text_file<CPose3DPDFGaussianInf>(const CNetworkOfPoses<CPose3DPDFGaussianInf> *g, const std::string &fil);

template void BASE_IMPEXP mrpt::poses::detail::load_graph_of_poses_from_text_file<CPosePDFGaussian>(CNetworkOfPoses<CPosePDFGaussian> *g, const std::string &fil);
template void BASE_IMPEXP mrpt::poses::detail::load_graph_of_poses_from_text_file<CPose3DPDFGaussian>(CNetworkOfPoses<CPose3DPDFGaussian> *g, const std::string &fil);
template void BASE_IMPEXP mrpt::poses::detail::load_graph_of_poses_from_text_file<CPosePDFGaussianInf>(CNetworkOfPoses<CPosePDFGaussianInf> *g, const std::string &fil);
template void BASE_IMPEXP mrpt::poses::detail::load_graph_of_poses_from_text_file<CPose3DPDFGaussianInf>(CNetworkOfPoses<CPose3DPDFGaussianInf> *g, const std::string &fil);



//   Implementation of serialization stuff
// --------------------------------------------------------------------------------
IMPLEMENTS_SERIALIZABLE( CNetworkOfPoses2D, CSerializable, mrpt::poses )
IMPLEMENTS_SERIALIZABLE( CNetworkOfPoses3D, CSerializable, mrpt::poses )
IMPLEMENTS_SERIALIZABLE( CNetworkOfPoses2DInf, CSerializable, mrpt::poses )
IMPLEMENTS_SERIALIZABLE( CNetworkOfPoses3DInf, CSerializable, mrpt::poses )

// Use MRPT's STL-metaprogramming automatic serialization for the field "edges" & "nodes":
#define DO_IMPLEMENT_READ_WRITE(_CLASS) \
	void  _CLASS::writeToStream(CStream &out, int *version) const \
	{ \
		if (version) \
			*version = 0; \
		else \
		{ \
			out << nodes << edges << root;  \
		} \
	} \
	void  _CLASS::readFromStream(CStream &in, int version) \
	{ \
		switch(version) \
		{ \
		case 0: \
			{ \
				in >> nodes >> edges >> root; \
			} break; \
		default: \
			MRPT_THROW_UNKNOWN_SERIALIZATION_VERSION(version) \
		}; \
	}


DO_IMPLEMENT_READ_WRITE(CNetworkOfPoses2D)
DO_IMPLEMENT_READ_WRITE(CNetworkOfPoses3D)
DO_IMPLEMENT_READ_WRITE(CNetworkOfPoses2DInf)
DO_IMPLEMENT_READ_WRITE(CNetworkOfPoses3DInf)

