#ifndef SCENE_HEADER_FILE
#define SCENE_HEADER_FILE

#include <vector>
#include <fstream>
#include <igl/bounding_box.h>
#include <igl/readMESH.h>
#include "ccd.h"
#include "volInt.h"
#include "auxfunctions.h"

using namespace Eigen;
using namespace std;


void support(const void *_obj, const ccd_vec3_t *_d, ccd_vec3_t *_p);
void stub_dir(const void *obj1, const void *obj2, ccd_vec3_t *dir);
void center(const void *_obj, ccd_vec3_t *dir);



//Impulse is defined as a pair <position, direction>
typedef std::pair<RowVector3d,RowVector3d> Impulse;


//the class the contains each individual rigid objects and their functionality
class Mesh{
public:
  MatrixXd origV;   //original vertex positions, where COM=(0.0,0.0,0.0) - never change this!
  MatrixXd currV;   //current vertex position
  MatrixXi F;   //faces of the tet mesh
  MatrixXi T;   //Tets in the tet mesh
  
  VectorXi boundTets;  //indices (from T) of just the boundary tets, for collision
  
  //position of object in space. We must always have that currV = QRot(origV, orientation)+ COM
  RowVector4d orientation; //current orientation
  RowVector3d COM;  //current center of mass
  Matrix3d invIT;  //Original *inverse* inertia tensor around the COM, defined in the rest state to the object (so to the canonical world system)
  
  VectorXd tetVolumes;    //|T|x1 tetrahedra volumes
  VectorXd invMasses;     //|T|x1 tetrahedra *inverse* masses
    
    bool user_c = false;
    bool is_wind_generator = false;
  
  //kinematics
  bool isFixed;  //is the object immobile
  double totalMass;  //sum(1/invMass)
  double totalVolume;
  RowVector3d comVelocity;  //the linear velocity of the center of mass
  RowVector3d angVelocity;  //the angular velocity of the object.
  
  //dynamics
  std::vector<Impulse> currImpulses;  //current list of impulses, updated by collision handling
  
  //checking collision between bounding boxes, and consequently the boundary tets if succeeds.
  //you do not need to update these functions (isBoxCollide and isCollide) unless you are doing a different collision
  
  bool isBoxCollide(const Mesh& m){
    RowVector3d VMin1=currV.colwise().minCoeff();
    RowVector3d VMax1=currV.colwise().maxCoeff();
    RowVector3d VMin2=m.currV.colwise().minCoeff();
    RowVector3d VMax2=m.currV.colwise().maxCoeff();
    
    //checking all axes for non-intersection of the dimensional interval
    for (int i=0;i<3;i++)
      if ((VMax1(i)<VMin2(i))||(VMax2(i)<VMin1(i)))
        return false;
    
    return true;  //all dimensional intervals are overlapping = intersection
    
  }
  
  bool isCollide(const Mesh& m, double& depth, RowVector3d& intNormal, RowVector3d& intPosition){
    
    
    if ((isFixed && m.isFixed))  //collision does nothing
      return false;
    
    //collision between bounding boxes
    if (!isBoxCollide(m))
      return false;
    
    //otherwise, full test
    ccd_t ccd;
    CCD_INIT(&ccd);
    ccd.support1       = support; // support function for first object
    ccd.support2       = support; // support function for second object
    ccd.center1         =center;
    ccd.center2         =center;
    
    ccd.first_dir       = stub_dir;
    ccd.max_iterations = 100;     // maximal number of iterations
    
    
    void* obj1=(void*)this;
    void* obj2=(void*)&m;
    
    ccd_real_t _depth;
    ccd_vec3_t dir, pos;
    
    int nonintersect = ccdMPRPenetration(obj1, obj2, &ccd, &_depth, &dir, &pos);
    
    if (nonintersect)
      return false;
    
    for (int k=0;k<3;k++){
      intNormal(k)=dir.v[k];
      intPosition(k)=pos.v[k];
    }
    
    depth =_depth;
    intPosition-=depth*intNormal/2.0;
    
    //Vector3d p1=intPosition+depth*intNormal;
    //Vector3d p2=intPosition;
    //std::cout<<"intPosition: "<<intPosition<<std::endl;
    
    //std::cout<<"depth: "<<depth<<std::endl;
    //std::cout<<"After ccdGJKIntersect"<<std::endl;
    
    //return !nonintersect;
    
    return true;
    
  }
  
  
  //return the current inverted inertia tensor around the current COM. Update it by applying the orientation
  Matrix3d getCurrInvInertiaTensor(){
    Matrix3d R=Q2RotMatrix(orientation);
      
      //Update the inverted inertia tensor by applying the orientation
      
      invIT = (R.transpose())*invIT*R;
      return invIT;
      //added
    
  }
  
  
  //Update the current position and orientation by integrating the linear and angular velocities, and update currV accordingly
  //You need to modify this according to its purpose
  void updatePosition(double timeStep){
    //just forward Euler now
    if (isFixed)
      return;  //a fixed object is immobile
      
      //added
      //update position using comVelocity (linear velocity)
      COM += (comVelocity*timeStep);
      //update orientation using angVelocity (the angular velocity)
      RowVector4d temp_w(0,angVelocity(0),angVelocity(1),angVelocity(2));
      orientation += 0.5 * timeStep * QMult(temp_w,orientation);
    
    for (int i=0;i<currV.rows();i++)
      currV.row(i)<<QRot(origV.row(i), orientation)+COM;
  }
  
  
  //Updating velocity *instantaneously*. i.e., not integration from acceleration, but as a result of a collision impulse from the "impulses" list
  //You need to modify this for that purpose.
  void updateImpulseVelocities(){
    
    if (isFixed){
      comVelocity.setZero();
      currImpulses.clear();
      angVelocity.setZero();
      return;
    }
      
    //Added: update linear and angular velocity according to all impulses
    for(int i = 0; i<currImpulses.size();i++){
        
        //contact pos
        RowVector3d pos = currImpulses[i].first;
        //jn
        RowVector3d dir = currImpulses[i].second;
        
        RowVector3d collision_arm = (pos - COM);
        
        RowVector3d r_jn = collision_arm.cross(dir);
        
        comVelocity += (dir / totalMass);
        angVelocity += invIT*(r_jn.transpose());
       // std::cout<<"angVelocity: "<<angVelocity<<std::endl;
    }
    //clear the impulses when done applying them
    currImpulses.clear();
    
  }
  
  RowVector3d initStaticProperties(const double density)
  {
    //TODO: compute tet volumes and allocate to vertices
    tetVolumes.conservativeResize(T.rows());
    
    RowVector3d naturalCOM; naturalCOM.setZero();
    Matrix3d IT; IT.setZero();
    for (int i=0;i<T.rows();i++){
      Vector3d e01=origV.row(T(i,1))-origV.row(T(i,0));
      Vector3d e02=origV.row(T(i,2))-origV.row(T(i,0));
      Vector3d e03=origV.row(T(i,3))-origV.row(T(i,0));
      Vector3d tetCentroid=(origV.row(T(i,0))+origV.row(T(i,1))+origV.row(T(i,2))+origV.row(T(i,3)))/4.0;
      tetVolumes(i)=std::abs(e01.dot(e02.cross(e03)))/6.0;
      
      naturalCOM+=tetVolumes(i)*tetCentroid;
      
    }
    
    totalVolume=tetVolumes.sum();
    totalMass=density*totalVolume;
    naturalCOM.array()/=totalVolume;
    
    //computing inertia tensor
    for (int i=0;i<T.rows();i++){
      RowVector4d xvec; xvec<<origV(T(i,0),0)-naturalCOM(0),origV(T(i,1),0)-naturalCOM(0),origV(T(i,2),0)-naturalCOM(0),origV(T(i,3),0)-naturalCOM(0);
      RowVector4d yvec; yvec<<origV(T(i,0),1)-naturalCOM(1),origV(T(i,1),1)-naturalCOM(1),origV(T(i,2),1)-naturalCOM(1),origV(T(i,3),1)-naturalCOM(1);
      RowVector4d zvec; zvec<<origV(T(i,0),2)-naturalCOM(2),origV(T(i,1),2)-naturalCOM(2),origV(T(i,2),2)-naturalCOM(2),origV(T(i,3),2)-naturalCOM(2);
      
      double I00, I11, I22, I12, I21, I01, I10, I02, I20;
      Matrix4d sumMat=Matrix4d::Constant(1.0)+Matrix4d::Identity();
      I00 = density*6*tetVolumes(i)*(yvec*sumMat*yvec.transpose()+zvec*sumMat*zvec.transpose()).sum()/120.0;
      I11 = density*6*tetVolumes(i)*(xvec*sumMat*xvec.transpose()+zvec*sumMat*zvec.transpose()).sum()/120.0;
      I22 = density*6*tetVolumes(i)*(xvec*sumMat*xvec.transpose()+yvec*sumMat*yvec.transpose()).sum()/120.0;
      I12 = I21 = -density*6*tetVolumes(i)*(yvec*sumMat*zvec.transpose()).sum()/120.0;
      I10 = I01 = -density*6*tetVolumes(i)*(xvec*sumMat*zvec.transpose()).sum()/120.0;
      I20 = I02 = -density*6*tetVolumes(i)*(xvec*sumMat*yvec.transpose()).sum()/120.0;
      
      Matrix3d currIT; currIT<<I00, I01, I02,
      I10, I11, I12,
      I20, I21, I22;
      
      IT+=currIT;
      
    }
    invIT=IT.inverse();
  
    return naturalCOM;
    
  }
  
  
  //Integrating the linear and angular velocities of the object
  //You need to modify this to integrate from acceleration in the field (basically gravity)
  void updateVelocity(double timeStep, double DragCoeff,int move_dir){
    
    if (isFixed)
      return;
    
      if(user_c){
          if(move_dir == 2) {
              cout << "left" << endl;
              comVelocity = RowVector3d(0.0,0.0,0.0);
              angVelocity = RowVector3d(0.0,4.0,0.0);
          }
          else if(move_dir == 3) {
              cout << "right" << endl;
              comVelocity = RowVector3d(0.0,0.0,0.0);
              angVelocity = RowVector3d(0.0,-4.0,0.0);
          }
          else if(move_dir == 1) {
              cout << "forward" << endl;
              Matrix3d R=Q2RotMatrix(orientation);
              angVelocity = RowVector3d(0.0,0.0,0.0);
              comVelocity += timeStep* R*RowVector3d(12.0,0.0,0.0).transpose();
              //   angVelocity = (RowVector3d(0.0,0.0,-4.0));
          }
          else if(move_dir == 5) {
              cout << "handbrake" << endl;
              angVelocity = RowVector3d(0.0,0.0,0.0);
              comVelocity = RowVector3d(0.0,0.0,0.0);
          }
          else {
              angVelocity = RowVector3d(0.0,0.0,0.0);
           //   comVelocity = RowVector3d(0.0,0.0,0.0);
          }
      }
      
    //integrating external forces (only gravity)
      Vector3d gravity; gravity<<0,-9.8,0.0;
      

      if(user_c){
          cout << "cout --->" << COM << endl;
      }
      
      Vector3d wind; wind<<0,0,10.0;
      if(COM(0) <= 150 && COM(0) >= -150){
          
          if(COM(2) < -100) comVelocity+= wind*timeStep;
          if(COM(2) > 100) comVelocity+= -1*wind*timeStep;
          
          
          if(COM(2) < -400) comVelocity+= 2*wind*timeStep;
          if(COM(2) > 450) comVelocity+= -2*wind*timeStep;
      }
      
      if(is_wind_generator){
          angVelocity = RowVector3d(0.0,0.0,-4.0);
          comVelocity = RowVector3d(0.0,0.0,0.0);
          Vector3d gravity; gravity<<0,0,0.0;
      }
    //  Vector3d gravity; gravity<<0,0.0,0.0;
    comVelocity+=gravity*timeStep;
      
     // std::cout<<"angVelocity: "<<angVelocity<<std::endl;
    Vector3d r = COM - COM;
      for (int i=0;i<currV.rows();i++){
          r = currV.row(i) - COM;
          break;
      }
      
    //Vector3d t = comVelocity + angVelocity.cross(r);
    //Vector3d tt = (-1 * DragCoeff) * t;
      
    //dragg
    comVelocity += (-1 * DragCoeff) * comVelocity;
    Vector3d linear_component_angular =(-1 * DragCoeff) * angVelocity.cross(r);
    if(!(r.x() == 0 && r.y() == 0 && r.z() == 0)) angVelocity += (r.cross(linear_component_angular)) / (r.dot(r));
  }
  
  
  //the full integration for the time step (velocity + position)
  //You need to modify this if you are changing the integration
  void integrate(double timeStep, double DragCoeff, int move_dir){
    updateVelocity(timeStep, DragCoeff,move_dir);
    updatePosition(timeStep);
  }
  
  
  Mesh(const MatrixXd& _V, const MatrixXi& _F, const MatrixXi& _T, const double density, const bool _isFixed, const RowVector3d& _COM, const RowVector4d& _orientation, const bool user_C, const bool iwg){
    origV=_V;
    F=_F;
    T=_T;
    isFixed=_isFixed;
    COM=_COM;
    orientation=_orientation;
    comVelocity.setZero();
    angVelocity.setZero();
    
      user_c = user_C;
      is_wind_generator = iwg;
    RowVector3d naturalCOM;  //by the geometry of the object
    
    //initializes the original geometric properties (COM + IT) of the object
    naturalCOM = initStaticProperties(density);
    
    origV.rowwise()-=naturalCOM;  //removing the natural COM of the OFF file (natural COM is never used again)
    
    currV.resize(origV.rows(), origV.cols());
    for (int i=0;i<currV.rows();i++)
      currV.row(i)<<QRot(origV.row(i), orientation)+COM;
    
    
    VectorXi boundVMask(origV.rows());
    boundVMask.setZero();
    for (int i=0;i<F.rows();i++)
      for (int j=0;j<3;j++)
        boundVMask(F(i,j))=1;
    
    //cout<<"boundVMask.sum(): "<<boundVMask.sum()<<endl;
    
    vector<int> boundTList;
    for (int i=0;i<T.rows();i++){
      int incidence=0;
      for (int j=0;j<4;j++)
        incidence+=boundVMask(T(i,j));
      if (incidence>2)
        boundTList.push_back(i);
    }
    
    boundTets.resize(boundTList.size());
    for (int i=0;i<boundTets.size();i++)
      boundTets(i)=boundTList[i];
  }
  
  ~Mesh(){}
};

//This class contains the entire scene operations, and the engine time loop.
class Scene{
public:
  double currTime;
  int numFullV, numFullT;
  std::vector<Mesh> meshes;
  
  //adding an objects. You do not need to update this generally
  void addMesh(const MatrixXd& V, const MatrixXi& F, const MatrixXi& T, const double density, const bool isFixed, const RowVector3d& COM, const RowVector4d& orientation,bool user_C = false,bool is_wind_generator = false){
    
    Mesh m(V,F, T, density, isFixed, COM, orientation,user_C,is_wind_generator);
    meshes.push_back(m);
  }
  
  /*********************************************************************
   This function handles a collision between objects ro1 and ro2 when found, by assigning impulses to both objects.
   Input: RigidObjects m1, m2
   depth: the depth of penetration
   contactNormal: the normal of the conact measured m1->m2
   penPosition: a point on m2 such that if m2 <= m2 + depth*contactNormal, then penPosition+depth*contactNormal is the common contact point
   CRCoeff: the coefficient of restitution
   *********************************************************************/
  void handleCollision(Mesh& m1, Mesh& m2,const double& depth, const RowVector3d& contactNormal,const RowVector3d& penPosition, const double CRCoeff, const double FrictionCoef){
    
    
   // std::cout<<"contactNormal: "<<contactNormal<<std::endl;
   // std::cout<<"penPosition: "<<penPosition<<std::endl;
    //std::cout<<"handleCollision begin"<<std::endl;
    
    
    //Interpretation resolution: move each object by inverse mass weighting, unless either is fixed, and then move the other. Remember to respect the direction of contactNormal and update penPosition accordingly.
    RowVector3d contactPosition;
    if (m1.isFixed){
        
        //Only move m2
        m2.COM = m2.COM + (depth * contactNormal);
    } else if (m2.isFixed){
        
        //Only move m1
        m1.COM = m1.COM - (depth * contactNormal);

    } else { //inverse mass weighting
        
        
        double totalmass = m1.totalMass + m2.totalMass;
        m1.COM = m1.COM - ((depth * (m2.totalMass/totalmass)) * contactNormal);
        m2.COM = m2.COM + ((depth * (m1.totalMass/totalmass)) * contactNormal);
    }
    
    
    //Create impulse and push them into m1.impulses and m2.impulses.
    contactPosition = penPosition+depth*contactNormal;
      
    RowVector3d r_a = contactPosition - m1.COM;
    RowVector3d r_b = contactPosition - m2.COM;
      
    RowVector3d c_a = m1.comVelocity + (m1.angVelocity.cross(r_a));
    RowVector3d c_b = m2.comVelocity + (m2.angVelocity.cross(r_b));
      
    double j_upper = (-1 * (1 + CRCoeff)) * ((c_a - c_b).dot(contactNormal));
      
    RowVector3d ra_n = (r_a.cross(contactNormal));
    RowVector3d rb_n = (r_b.cross(contactNormal));
      
      double tt_ = ra_n * m1.invIT * ra_n.transpose();
      
      tt_ += rb_n * m2.invIT * rb_n.transpose();
      
    double j_lower = (1/m1.totalMass) + (1/m2.totalMass) + tt_;
     double j = j_upper / j_lower;
      
      RowVector3d temp_impulse;
      
      
      //calculate friction
      RowVector3d tangential_t = (contactNormal.cross(c_a - c_b)).cross(contactNormal);

      //only normalize if it is unequal to the zero vector
      if((tangential_t.x() + tangential_t.y() + tangential_t.z()) != 0) tangential_t.normalize();

      // instead of jn we get j(n - (u_k)t)
      temp_impulse = -1 * (j * ((contactNormal) + (FrictionCoef*tangential_t)));
      

    
    RowVector3d impulse=RowVector3d::Zero();  //change this to your result
      impulse = temp_impulse;
    
  //  std::cout<<"impulse: "<<impulse<<std::endl;
    if (impulse.norm()>10e-6){
      m1.currImpulses.push_back(Impulse(contactPosition, -impulse));
      m2.currImpulses.push_back(Impulse(contactPosition, impulse));
    }
    
    //updating velocities according to impulses
    m1.updateImpulseVelocities();
    m2.updateImpulseVelocities();
  }
  
  
  
  /*********************************************************************
   This function handles a single time step by:
   1. Integrating velocities, positions, and orientations by the timeStep
   2. detecting and handling collisions with the coefficient of restitutation CRCoeff
   3. updating the visual scene in fullV and fullT
   *********************************************************************/
  void updateScene(double timeStep, double CRCoeff, double DragCoeff, double FrictionCoef, int move_dir = 0){
    
    //integrating velocity, position and orientation from forces and previous states
    for (int i=0;i<meshes.size();i++)
      meshes[i].integrate(timeStep, DragCoeff,move_dir);
    
    //detecting and handling collisions when found
    //This is done exhaustively: checking every two objects in the scene.
    double depth;
    RowVector3d contactNormal, penPosition;
    for (int i=0;i<meshes.size();i++)
      for (int j=i+1;j<meshes.size();j++)
        if (meshes[i].isCollide(meshes[j],depth, contactNormal, penPosition))
          handleCollision(meshes[i], meshes[j],depth, contactNormal, penPosition,CRCoeff, FrictionCoef);
    
    currTime+=timeStep;
  }
  
  //loading a scene from the scene .txt files
  //you do not need to update this function
  bool loadScene(const std::string dataFolder, const std::string sceneFileName){
    
    ifstream sceneFileHandle;
    sceneFileHandle.open(dataFolder+std::string("/")+sceneFileName);
    if (!sceneFileHandle.is_open())
      return false;
    int numofObjects;
    
    currTime=0;
    sceneFileHandle>>numofObjects;
    bool first_mesh_load = true;
    for (int i=0;i<numofObjects;i++){
      MatrixXi objT, objF;
      MatrixXd objV;
      std::string MESHFileName;
      bool isFixed;
      double youngModulus, poissonRatio, density;
      RowVector3d userCOM;
      RowVector4d userOrientation;
      sceneFileHandle>>MESHFileName>>density>>youngModulus>>poissonRatio>>isFixed>>userCOM(0)>>userCOM(1)>>userCOM(2)>>userOrientation(0)>>userOrientation(1)>>userOrientation(2)>>userOrientation(3);
      userOrientation.normalize();
      igl::readMESH(dataFolder+std::string("/")+MESHFileName,objV,objT, objF);
      
      //fixing weird orientation problem
      MatrixXi tempF(objF.rows(),3);
      tempF<<objF.col(2), objF.col(1), objF.col(0);
      objF=tempF;
      
        if(first_mesh_load){
            first_mesh_load = false;
            addMesh(objV,objF, objT,density, isFixed, userCOM, userOrientation,true);
        }
      else addMesh(objV,objF, objT,density, isFixed, userCOM, userOrientation);
      cout << "COM: " << userCOM <<endl;
      cout << "orientation: " << userOrientation <<endl;
    }
    return true;
  }
  
  
  Scene(){}
  ~Scene(){}
};


/*****************************Auxiliary functions for collision detection. Do not need updating********************************/

/** Support function for libccd*/
void support(const void *_obj, const ccd_vec3_t *_d, ccd_vec3_t *_p)
{
  // assume that obj_t is user-defined structure that holds info about
  // object (in this case box: x, y, z, pos, quat - dimensions of box,
  // position and rotation)
  //std::cout<<"calling support"<<std::endl;
  Mesh *obj = (Mesh *)_obj;
  RowVector3d p;
  RowVector3d d;
  for (int i=0;i<3;i++)
    d(i)=_d->v[i]; //p(i)=_p->v[i];
  
  d.normalize();
  //std::cout<<"d: "<<d<<std::endl;
  
  int maxVertex=-1;
  int maxDotProd=-32767.0;
  for (int i=0;i<obj->currV.rows();i++){
    double currDotProd=d.dot(obj->currV.row(i)-obj->COM);
    if (maxDotProd < currDotProd){
      maxDotProd=currDotProd;
      //std::cout<<"maxDotProd: "<<maxDotProd<<std::endl;
      maxVertex=i;
    }
    
  }
  //std::cout<<"maxVertex: "<<maxVertex<<std::endl;
  
  for (int i=0;i<3;i++)
    _p->v[i]=obj->currV(maxVertex,i);
  
  //std::cout<<"end support"<<std::endl;
}

void stub_dir(const void *obj1, const void *obj2, ccd_vec3_t *dir)
{
  dir->v[0]=1.0;
  dir->v[1]=0.0;
  dir->v[2]=0.0;
}

void center(const void *_obj,ccd_vec3_t *center)
{
  Mesh *obj = (Mesh *)_obj;
  for (int i=0;i<3;i++)
    center->v[i]=obj->COM(i);
}








#endif
