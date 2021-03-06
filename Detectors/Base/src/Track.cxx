// Copyright CERN and copyright holders of ALICE O2. This software is
// distributed under the terms of the GNU General Public License v3 (GPL
// Version 3), copied verbatim in the file "COPYING".
//
// See http://alice-o2.web.cern.ch/license for full licensing information.
//
// In applying this license CERN does not waive the privileges and immunities
// granted to it by virtue of its status as an Intergovernmental Organization
// or submit itself to any jurisdiction.

#include "DetectorsBase/Track.h"

using std::array;
using o2::Base::Track::TrackParBase;
using o2::Base::Track::TrackPar;
using o2::Base::Track::TrackParCov;
using namespace o2::Base::Constants;

ClassImp(o2::Base::Track::TrackParBase);
ClassImp(o2::Base::Track::TrackParCov);
ClassImp(o2::Base::Track::TrackPar);

//______________________________________________________________
TrackParBase::TrackParBase(const array<float, 3>& xyz, const array<float, 3>& pxpypz, int charge, bool sectorAlpha)
  : mX{ 0.f }, mAlpha{ 0.f }, mP{ 0.f }
{
  // construct track param from kinematics

  // Alpha of the frame is defined as:
  // sectorAlpha == false : -> angle of pt direction
  // sectorAlpha == true  : -> angle of the sector from X,Y coordinate for r>1
  //                           angle of pt direction for r==0
  //
  //
  constexpr float kSafe = 1e-5f;
  float radPos2 = xyz[0] * xyz[0] + xyz[1] * xyz[1];
  float alp = 0;
  if (sectorAlpha || radPos2 < 1) {
    alp = atan2f(pxpypz[1], pxpypz[0]);
  } else {
    alp = atan2f(xyz[1], xyz[0]);
  }
  if (sectorAlpha) {
    alp = Utils::Angle2Alpha(alp);
  }
  //
  float sn, cs;
  Utils::sincosf(alp, sn, cs);
  // protection:  avoid alpha being too close to 0 or +-pi/2
  if (fabs(sn) < 2 * kSafe) {
    if (alp > 0) {
      alp += alp < kPIHalf ? 2 * kSafe : -2 * kSafe;
    } else {
      alp += alp > -kPIHalf ? -2 * kSafe : 2 * kSafe;
    }
    Utils::sincosf(alp, sn, cs);
  } else if (fabs(cs) < 2 * kSafe) {
    if (alp > 0) {
      alp += alp > kPIHalf ? 2 * kSafe : -2 * kSafe;
    } else {
      alp += alp > -kPIHalf ? 2 * kSafe : -2 * kSafe;
    }
    Utils::sincosf(alp, sn, cs);
  }
  // Get the vertex of origin and the momentum
  array<float, 3> ver{ xyz[0], xyz[1], xyz[2] };
  array<float, 3> mom{ pxpypz[0], pxpypz[1], pxpypz[2] };
  //
  // Rotate to the local coordinate system
  Utils::RotateZ(ver, -alp);
  Utils::RotateZ(mom, -alp);
  //
  float ptI = 1.f / sqrt(mom[0] * mom[0] + mom[1] * mom[1]);
  mX = ver[0];
  mAlpha = alp;
  mP[kY] = ver[1];
  mP[kZ] = ver[2];
  mP[kSnp] = mom[1] * ptI;
  mP[kTgl] = mom[2] * ptI;
  mP[kQ2Pt] = ptI * charge;
  //
  if (fabs(1 - GetSnp()) < kSafe) {
    mP[kSnp] = 1. - kSafe; // Protection
  } else if (fabs(-1 - GetSnp()) < kSafe) {
    mP[kSnp] = -1. + kSafe; // Protection
  }
  //
}

//_______________________________________________________
bool TrackParBase::GetPxPyPzGlo(array<float, 3>& pxyz) const
{
  // track momentum
  if (fabs(GetQ2Pt()) < kAlmost0 || fabs(GetSnp()) > kAlmost1) {
    return false;
  }
  float cs, sn, pt = fabs(1.f / GetQ2Pt());
  float r = sqrtf((1.f - GetSnp()) * (1.f + GetSnp()));
  Utils::sincosf(GetAlpha(), sn, cs);
  pxyz[0] = pt * (r * cs - GetSnp() * sn);
  pxyz[1] = pt * (GetSnp() * cs + r * sn);
  pxyz[2] = pt * GetTgl();
  return true;
}

//____________________________________________________
bool TrackParBase::GetPosDirGlo(array<float, 9>& posdirp) const
{
  // fill vector with lab x,y,z,px/p,py/p,pz/p,p,sinAlpha,cosAlpha
  float ptI = fabs(GetQ2Pt());
  float snp = GetSnp();
  if (ptI < kAlmost0 || fabs(snp) > kAlmost1) {
    return false;
  }
  float &sn = posdirp[7], &cs = posdirp[8];
  float csp = sqrtf((1.f - snp) * (1.f + snp));
  float cstht = sqrtf(1.f + GetTgl() * GetTgl());
  float csthti = 1.f / cstht;
  Utils::sincosf(GetAlpha(), sn, cs);
  posdirp[0] = GetX() * cs - GetY() * sn;
  posdirp[1] = GetX() * sn + GetY() * cs;
  posdirp[2] = GetZ();
  posdirp[3] = (csp * cs - snp * sn) * csthti; // px/p
  posdirp[4] = (snp * cs + csp * sn) * csthti; // py/p
  posdirp[5] = GetTgl() * csthti;              // pz/p
  posdirp[6] = cstht / ptI;                    // p
  return true;
}

//______________________________________________________________
bool TrackParBase::RotateParam(float alpha)
{
  // rotate to alpha frame
  if (fabs(GetSnp()) > kAlmost1) {
    // FairLogger::GetLogger()->Error(MESSAGE_ORIGIN,
    printf("Precondition is not satisfied: |sin(phi)|>1 ! %f\n", GetSnp());
    return false;
  }
  //
  Utils::BringToPMPi(alpha);
  //
  float ca = 0, sa = 0;
  Utils::sincosf(alpha - GetAlpha(), sa, ca);
  float snp = GetSnp(), csp = sqrtf((1.f - snp) * (1.f + snp)); // Improve precision
  // RS: check if rotation does no invalidate track model (cos(local_phi)>=0, i.e. particle
  // direction in local frame is along the X axis
  if ((csp * ca + snp * sa) < 0) {
    // FairLogger::GetLogger()->Warning(MESSAGE_ORIGIN,
    printf("Rotation failed: local cos(phi) would become %.2f\n", csp * ca + snp * sa);
    return false;
  }
  //
  float tmp = snp * ca - csp * sa;
  if (fabs(tmp) > kAlmost1) {
    // FairLogger::GetLogger()->Warning(MESSAGE_ORIGIN,
    printf("Rotation failed: new snp %.2f\n", tmp);
    return false;
  }
  float xold = GetX(), yold = GetY();
  mAlpha = alpha;
  mX = xold * ca + yold * sa;
  mP[kY] = -xold * sa + yold * ca;
  mP[kSnp] = tmp;
  return true;
}

//____________________________________________________________
bool TrackParBase::PropagateParamTo(float xk, const array<float, 3>& b)
{
  //----------------------------------------------------------------
  // Extrapolate this track params (w/o cov matrix) to the plane X=xk in the field b[].
  //
  // X [cm] is in the "tracking coordinate system" of this track.
  // b[]={Bx,By,Bz} [kG] is in the Global coordidate system.
  //----------------------------------------------------------------

  float dx = xk - GetX();
  if (fabs(dx) < kAlmost0) {
    return true;
  }
  // Do not propagate tracks outside the ALICE detector
  if (fabs(dx) > 1e5 || fabs(GetY()) > 1e5 || fabs(GetZ()) > 1e5) {
    printf("Anomalous track, target X:%f\n", xk);
    //    Print();
    return false;
  }
  float crv = (fabs(b[2]) < kAlmost0) ? 0.f : GetCurvature(b[2]);
  float x2r = crv * dx;
  float f1 = GetSnp(), f2 = f1 + x2r;
  if (fabs(f1) > kAlmost1 || fabs(f2) > kAlmost1) {
    return false;
  }
  if (fabs(GetQ2Pt()) < kAlmost0) {
    return false;
  }
  float r1 = sqrtf((1.f - f1) * (1.f + f1));
  if (fabs(r1) < kAlmost0) {
    return false;
  }
  float r2 = sqrtf((1.f - f2) * (1.f + f2));
  if (fabs(r2) < kAlmost0) {
    return false;
  }
  float dy2dx = (f1 + f2) / (r1 + r2);
  float step = (fabs(x2r) < 0.05f) ? dx * fabs(r2 + f2 * dy2dx)                                       // chord
                                   : 2.f * asinf(0.5f * dx * sqrtf(1.f + dy2dx * dy2dx) * crv) / crv; // arc
  step *= sqrtf(1.f + GetTgl() * GetTgl());
  //
  // Get the track x,y,z,px/p,py/p,pz/p,p,sinAlpha,cosAlpha in the Global System
  array<float, 9> vecLab{ 0.f };
  if (!GetPosDirGlo(vecLab)) {
    return false;
  }

  // Rotate to the system where Bx=By=0.
  float bxy2 = b[0] * b[0] + b[1] * b[1];
  float bt = sqrtf(bxy2);
  float cosphi = 1.f, sinphi = 0.f;
  if (bt > kAlmost0) {
    cosphi = b[0] / bt;
    sinphi = b[1] / bt;
  }
  float bb = sqrtf(bxy2 + b[2] * b[2]);
  float costet = 1., sintet = 0.;
  if (bb > kAlmost0) {
    costet = b[2] / bb;
    sintet = bt / bb;
  }
  array<float, 7> vect{ costet * cosphi * vecLab[0] + costet * sinphi * vecLab[1] - sintet * vecLab[2],
                        -sinphi * vecLab[0] + cosphi * vecLab[1],
                        sintet * cosphi * vecLab[0] + sintet * sinphi * vecLab[1] + costet * vecLab[2],
                        costet * cosphi * vecLab[3] + costet * sinphi * vecLab[4] - sintet * vecLab[5],
                        -sinphi * vecLab[3] + cosphi * vecLab[4],
                        sintet * cosphi * vecLab[3] + sintet * sinphi * vecLab[4] + costet * vecLab[5],
                        vecLab[6] };

  // Do the helix step
  float sgn = GetSign();
  g3helx3(sgn * bb, step, vect);

  // Rotate back to the Global System
  vecLab[0] = cosphi * costet * vect[0] - sinphi * vect[1] + cosphi * sintet * vect[2];
  vecLab[1] = sinphi * costet * vect[0] + cosphi * vect[1] + sinphi * sintet * vect[2];
  vecLab[2] = -sintet * vect[0] + costet * vect[2];

  vecLab[3] = cosphi * costet * vect[3] - sinphi * vect[4] + cosphi * sintet * vect[5];
  vecLab[4] = sinphi * costet * vect[3] + cosphi * vect[4] + sinphi * sintet * vect[5];
  vecLab[5] = -sintet * vect[3] + costet * vect[5];

  // Rotate back to the Tracking System
  float sinalp = -vecLab[7], cosalp = vecLab[8];
  float t = cosalp * vecLab[0] - sinalp * vecLab[1];
  vecLab[1] = sinalp * vecLab[0] + cosalp * vecLab[1];
  vecLab[0] = t;
  t = cosalp * vecLab[3] - sinalp * vecLab[4];
  vecLab[4] = sinalp * vecLab[3] + cosalp * vecLab[4];
  vecLab[3] = t;

  // Do the final correcting step to the target plane (linear approximation)
  float x = vecLab[0], y = vecLab[1], z = vecLab[2];
  if (fabs(dx) > kAlmost0) {
    if (fabs(vecLab[3]) < kAlmost0) {
      return false;
    }
    dx = xk - vecLab[0];
    x += dx;
    y += vecLab[4] / vecLab[3] * dx;
    z += vecLab[5] / vecLab[3] * dx;
  }

  // Calculate the track parameters
  t = 1.f / sqrtf(vecLab[3] * vecLab[3] + vecLab[4] * vecLab[4]);
  mX = x;
  mP[kY] = y;
  mP[kZ] = z;
  mP[kSnp] = vecLab[4] * t;
  mP[kTgl] = vecLab[5] * t;
  mP[kQ2Pt] = sgn * t / vecLab[6];

  return true;
}

//____________________________________________________________
bool TrackParBase::PropagateParamTo(float xk, float b)
{
  //----------------------------------------------------------------
  // Propagate this track to the plane X=xk (cm) in the field "b" (kG)
  // Only parameters are propagated, not the matrix. To be used for small
  // distances only (<mm, i.e. misalignment)
  //----------------------------------------------------------------
  float dx = xk - GetX();
  if (fabs(dx) < kAlmost0) {
    return true;
  }
  float crv = (fabs(b) < kAlmost0) ? 0.f : GetCurvature(b);
  float x2r = crv * dx;
  float f1 = GetSnp(), f2 = f1 + x2r;
  if ((fabs(f1) > kAlmost1) || (fabs(f2) > kAlmost1) || (fabs(GetQ2Pt()) < kAlmost0)) {
    return false;
  }
  float r1 = sqrtf((1.f - f1) * (1.f + f1));
  if (fabs(r1) < kAlmost0) {
    return false;
  }
  float r2 = sqrtf((1.f - f2) * (1.f + f2));
  if (fabs(r2) < kAlmost0) {
    return false;
  }
  mX = xk;
  double dy2dx = (f1 + f2) / (r1 + r2);
  mP[kY] += dx * dy2dx;
  mP[kSnp] += x2r;
  if (fabs(x2r) < 0.05f) {
    mP[kZ] += dx * (r2 + f2 * dy2dx) * GetTgl();
  } else {
    // for small dx/R the linear apporximation of the arc by the segment is OK,
    // but at large dx/R the error is very large and leads to incorrect Z propagation
    // angle traversed delta = 2*asin(dist_start_end / R / 2), hence the arc is: R*deltaPhi
    // The dist_start_end is obtained from sqrt(dx^2+dy^2) = x/(r1+r2)*sqrt(2+f1*f2+r1*r2)
    //    double chord = dx*TMath::Sqrt(1+dy2dx*dy2dx);   // distance from old position to new one
    //    double rot = 2*TMath::ASin(0.5*chord*crv); // angular difference seen from the circle center
    //    track1 += rot/crv*track3;
    //
    float rot = asinf(r1 * f2 - r2 * f1);           // more economic version from Yura.
    if (f1 * f1 + f2 * f2 > 1.f && f1 * f2 < 0.f) { // special cases of large rotations or large abs angles
      if (f2 > 0.f) {
        rot = kPI - rot; //
      } else {
        rot = -kPI - rot;
      }
    }
    mP[kZ] += GetTgl() / crv * rot;
  }
  return true;
}

//____________________________________________________________
bool TrackParBase::GetYZAt(float xk, float b, float& y, float& z) const
{
  //----------------------------------------------------------------
  // estimate Y,Z in tracking frame at given X
  //----------------------------------------------------------------
  float dx = xk - GetX();
  if (fabs(dx) < kAlmost0) {
    return true;
  }
  float crv = (fabs(b) < kAlmost0) ? 0.f : GetCurvature(b);
  float x2r = crv * dx;
  float f1 = GetSnp(), f2 = f1 + x2r;
  if ((fabs(f1) > kAlmost1) || (fabs(f2) > kAlmost1)) {
    return false;
  }
  if (fabs(GetQ2Pt()) < kAlmost0) {
    return false;
  }
  float r1 = sqrtf((1.f - f1) * (1.f + f1));
  if (fabs(r1) < kAlmost0) {
    return false;
  }
  float r2 = sqrtf((1.f - f2) * (1.f + f2));
  if (fabs(r2) < kAlmost0) {
    return false;
  }
  double dy2dx = (f1 + f2) / (r1 + r2);
  y = mP[kY] + dx * dy2dx;
  z = mP[kZ];
  if (fabs(x2r) < 0.05f) {
    z += dx * (r2 + f2 * dy2dx) * GetTgl();
  } else {
    // for small dx/R the linear apporximation of the arc by the segment is OK,
    // but at large dx/R the error is very large and leads to incorrect Z propagation
    // angle traversed delta = 2*asin(dist_start_end / R / 2), hence the arc is: R*deltaPhi
    // The dist_start_end is obtained from sqrt(dx^2+dy^2) = x/(r1+r2)*sqrt(2+f1*f2+r1*r2)
    //    double chord = dx*TMath::Sqrt(1+dy2dx*dy2dx);   // distance from old position to new one
    //    double rot = 2*TMath::ASin(0.5*chord*crv); // angular difference seen from the circle center
    //    track1 += rot/crv*track3;
    //
    float rot = asinf(r1 * f2 - r2 * f1);           // more economic version from Yura.
    if (f1 * f1 + f2 * f2 > 1.f && f1 * f2 < 0.f) { // special cases of large rotations or large abs angles
      if (f2 > 0.f) {
        rot = kPI - rot; //
      } else {
        rot = -kPI - rot;
      }
    }
    z += GetTgl() / crv * rot;
  }
  return true;
}

//______________________________________________________________
void TrackParBase::InvertParam()
{
  // Transform this track to the local coord. system rotated by 180 deg.
  mX = -mX;
  mAlpha += kPI;
  Utils::BringToPMPi(mAlpha);
  //
  mP[0] = -mP[0];
  mP[3] = -mP[3];
  mP[4] = -mP[4];
  //
}

//______________________________________________________________
void TrackParBase::PrintParam() const
{
  // print parameters
  printf("X:%+e Alp:%+e Par: %+e %+e %+e %+e %+e\n", GetX(), GetAlpha(), GetY(), GetZ(), GetSnp(), GetTgl(), GetQ2Pt());
}

//______________________________________________________________
void TrackParCov::Invert()
{
  // Transform this track to the local coord. system rotated by 180 deg.
  InvertParam();
  // since the fP1 and fP2 are not inverted, their covariances with others change sign
  mC[kSigZY] = -mC[kSigZY];
  mC[kSigSnpY] = -mC[kSigSnpY];
  mC[kSigTglZ] = -mC[kSigTglZ];
  mC[kSigTglSnp] = -mC[kSigTglSnp];
  mC[kSigQ2PtZ] = -mC[kSigQ2PtZ];
  mC[kSigQ2PtSnp] = -mC[kSigQ2PtSnp];
}

//______________________________________________________________
bool TrackParCov::PropagateTo(float xk, float b)
{
  //----------------------------------------------------------------
  // Propagate this track to the plane X=xk (cm) in the field "b" (kG)
  //----------------------------------------------------------------
  float dx = xk - GetX();
  if (fabs(dx) < kAlmost0) {
    return true;
  }
  float crv = (fabs(b) < kAlmost0) ? 0.f : GetCurvature(b);
  float x2r = crv * dx;
  float f1 = GetSnp(), f2 = f1 + x2r;
  if ((fabs(f1) > kAlmost1) || (fabs(f2) > kAlmost1) || (fabs(GetQ2Pt()) < kAlmost0)) {
    return false;
  }
  float r1 = sqrtf((1.f - f1) * (1.f + f1));
  if (fabs(r1) < kAlmost0) {
    return false;
  }
  float r2 = sqrtf((1.f - f2) * (1.f + f2));
  if (fabs(r2) < kAlmost0) {
    return false;
  }
  mX = xk;
  double dy2dx = (f1 + f2) / (r1 + r2);
  mP[kY] += dx * dy2dx;
  mP[kSnp] += x2r;
  if (fabs(x2r) < 0.05f) {
    mP[kZ] += dx * (r2 + f2 * dy2dx) * GetTgl();
  } else {
    // for small dx/R the linear apporximation of the arc by the segment is OK,
    // but at large dx/R the error is very large and leads to incorrect Z propagation
    // angle traversed delta = 2*asin(dist_start_end / R / 2), hence the arc is: R*deltaPhi
    // The dist_start_end is obtained from sqrt(dx^2+dy^2) = x/(r1+r2)*sqrt(2+f1*f2+r1*r2)
    //    double chord = dx*TMath::Sqrt(1+dy2dx*dy2dx);   // distance from old position to new one
    //    double rot = 2*TMath::ASin(0.5*chord*crv); // angular difference seen from the circle center
    //    mP1 += rot/crv*mP3;
    //
    float rot = asinf(r1 * f2 - r2 * f1);           // more economic version from Yura.
    if (f1 * f1 + f2 * f2 > 1.f && f1 * f2 < 0.f) { // special cases of large rotations or large abs angles
      if (f2 > 0.f) {
        rot = kPI - rot; //
      } else {
        rot = -kPI - rot;
      }
    }
    mP[kZ] += GetTgl() / crv * rot;
  }

  float &c00 = mC[kSigY2], &c10 = mC[kSigZY], &c11 = mC[kSigZ2], &c20 = mC[kSigSnpY], &c21 = mC[kSigSnpZ],
        &c22 = mC[kSigSnp2], &c30 = mC[kSigTglY], &c31 = mC[kSigTglZ], &c32 = mC[kSigTglSnp], &c33 = mC[kSigTgl2],
        &c40 = mC[kSigQ2PtY], &c41 = mC[kSigQ2PtZ], &c42 = mC[kSigQ2PtSnp], &c43 = mC[kSigQ2PtTgl],
        &c44 = mC[kSigQ2Pt2];

  // evaluate matrix in double prec.
  double rinv = 1. / r1;
  double r3inv = rinv * rinv * rinv;
  double f24 = dx * b * kB2C; // x2r/mC[kQ2Pt];
  double f02 = dx * r3inv;
  double f04 = 0.5 * f24 * f02;
  double f12 = f02 * GetTgl() * f1;
  double f14 = 0.5 * f24 * f12; // 0.5*f24*f02*GetTgl()*f1;
  double f13 = dx * rinv;

  // b = C*ft
  double b00 = f02 * c20 + f04 * c40, b01 = f12 * c20 + f14 * c40 + f13 * c30;
  double b02 = f24 * c40;
  double b10 = f02 * c21 + f04 * c41, b11 = f12 * c21 + f14 * c41 + f13 * c31;
  double b12 = f24 * c41;
  double b20 = f02 * c22 + f04 * c42, b21 = f12 * c22 + f14 * c42 + f13 * c32;
  double b22 = f24 * c42;
  double b40 = f02 * c42 + f04 * c44, b41 = f12 * c42 + f14 * c44 + f13 * c43;
  double b42 = f24 * c44;
  double b30 = f02 * c32 + f04 * c43, b31 = f12 * c32 + f14 * c43 + f13 * c33;
  double b32 = f24 * c43;

  // a = f*b = f*C*ft
  double a00 = f02 * b20 + f04 * b40, a01 = f02 * b21 + f04 * b41, a02 = f02 * b22 + f04 * b42;
  double a11 = f12 * b21 + f14 * b41 + f13 * b31, a12 = f12 * b22 + f14 * b42 + f13 * b32;
  double a22 = f24 * b42;

  // F*C*Ft = C + (b + bt + a)
  c00 += b00 + b00 + a00;
  c10 += b10 + b01 + a01;
  c20 += b20 + b02 + a02;
  c30 += b30;
  c40 += b40;
  c11 += b11 + b11 + a11;
  c21 += b21 + b12 + a12;
  c31 += b31;
  c41 += b41;
  c22 += b22 + b22 + a22;
  c32 += b32;
  c42 += b42;

  CheckCovariance();

  return true;
}

//______________________________________________________________
bool TrackParCov::Rotate(float alpha)
{
  // rotate to alpha frame
  if (fabs(GetSnp()) > kAlmost1) {
    // FairLogger::GetLogger()->Error(MESSAGE_ORIGIN,
    printf("Precondition is not satisfied: |sin(phi)|>1 ! %f\n", GetSnp());
    return false;
  }
  //
  Utils::BringToPMPi(alpha);
  //
  float ca = 0, sa = 0;
  Utils::sincosf(alpha - mAlpha, sa, ca);
  float snp = GetSnp(), csp = sqrtf((1.f - snp) * (1.f + snp)); // Improve precision
  // RS: check if rotation does no invalidate track model (cos(local_phi)>=0, i.e. particle
  // direction in local frame is along the X axis
  if ((csp * ca + snp * sa) < 0) {
    // FairLogger::GetLogger()->Warning(MESSAGE_ORIGIN,
    printf("Rotation failed: local cos(phi) would become %.2f\n", csp * ca + snp * sa);
    return false;
  }
  //
  float tmp = snp * ca - csp * sa;
  if (fabs(tmp) > kAlmost1) {
    // FairLogger::GetLogger()->Warning(MESSAGE_ORIGIN,
    printf("Rotation failed: new snp %.2f\n", tmp);
    return false;
  }
  float xold = GetX(), yold = GetY();
  mAlpha = alpha;
  mX = xold * ca + yold * sa;
  mP[kY] = -xold * sa + yold * ca;
  mP[kSnp] = tmp;

  if (fabs(csp) < kAlmost0) {
    printf("Too small cosine value %f\n", csp);
    csp = kAlmost0;
  }

  float rr = (ca + snp / csp * sa);

  mC[kSigY2] *= (ca * ca);
  mC[kSigZY] *= ca;
  mC[kSigSnpY] *= ca * rr;
  mC[kSigSnpZ] *= rr;
  mC[kSigSnp2] *= rr * rr;
  mC[kSigTglY] *= ca;
  mC[kSigTglSnp] *= rr;
  mC[kSigQ2PtY] *= ca;
  mC[kSigQ2PtSnp] *= rr;

  CheckCovariance();
  return true;
}

//______________________________________________________________
TrackParCov::TrackParCov(const array<float, 3>& xyz, const array<float, 3>& pxpypz,
                         const array<float, kLabCovMatSize>& cv, int charge, bool sectorAlpha)
{
  // construct track param and covariance from kinematics and lab errors

  // Alpha of the frame is defined as:
  // sectorAlpha == false : -> angle of pt direction
  // sectorAlpha == true  : -> angle of the sector from X,Y coordinate for r>1
  //                           angle of pt direction for r==0
  //
  //
  constexpr float kSafe = 1e-5f;
  float radPos2 = xyz[0] * xyz[0] + xyz[1] * xyz[1];
  float alp = 0;
  if (sectorAlpha || radPos2 < 1) {
    alp = atan2f(pxpypz[1], pxpypz[0]);
  } else {
    alp = atan2f(xyz[1], xyz[0]);
  }
  if (sectorAlpha) {
    alp = Utils::Angle2Alpha(alp);
  }
  //
  float sn, cs;
  Utils::sincosf(alp, sn, cs);
  // protection:  avoid alpha being too close to 0 or +-pi/2
  if (fabs(sn) < 2.f * kSafe) {
    if (alp > 0) {
      alp += alp < kPIHalf ? 2.f * kSafe : -2.f * kSafe;
    } else {
      alp += alp > -kPIHalf ? -2.f * kSafe : 2.f * kSafe;
    }
    Utils::sincosf(alp, sn, cs);
  } else if (fabs(cs) < 2.f * kSafe) {
    if (alp > 0) {
      alp += alp > kPIHalf ? 2.f * kSafe : -2.f * kSafe;
    } else {
      alp += alp > -kPIHalf ? 2.f * kSafe : -2.f * kSafe;
    }
    Utils::sincosf(alp, sn, cs);
  }
  // Get the vertex of origin and the momentum
  array<float, 3> ver{ xyz[0], xyz[1], xyz[2] };
  array<float, 3> mom{ pxpypz[0], pxpypz[1], pxpypz[2] };
  //
  // Rotate to the local coordinate system
  Utils::RotateZ(ver, -alp);
  Utils::RotateZ(mom, -alp);
  //
  float pt = sqrt(mom[0] * mom[0] + mom[1] * mom[1]);
  float ptI = 1.f / pt;
  mX = ver[0];
  mAlpha = alp;
  mP[kY] = ver[1];
  mP[kZ] = ver[2];
  mP[kSnp] = mom[1] * ptI; // cos(phi)
  mP[kTgl] = mom[2] * ptI; // tg(lambda)
  mP[kQ2Pt] = ptI * charge;
  //
  if (fabs(1.f - GetSnp()) < kSafe) {
    mP[kSnp] = 1.f - kSafe; // Protection
  } else if (fabs(-1.f - GetSnp()) < kSafe) {
    mP[kSnp] = -1.f + kSafe; // Protection
  }
  //
  // Covariance matrix (formulas to be simplified)
  float r = mom[0] * ptI; // cos(phi)
  float cv34 = sqrtf(cv[3] * cv[3] + cv[4] * cv[4]);
  //
  int special = 0;
  float sgcheck = r * sn + GetSnp() * cs;
  if (fabs(sgcheck) > 1 - kSafe) { // special case: lab phi is +-pi/2
    special = 1;
    sgcheck = sgcheck < 0 ? -1.f : 1.f;
  } else if (fabs(sgcheck) < kSafe) {
    sgcheck = cs < 0 ? -1.0f : 1.0f;
    special = 2; // special case: lab phi is 0
  }
  //
  mC[kSigY2] = cv[0] + cv[2];
  mC[kSigZY] = (-cv[3] * sn) < 0 ? -cv34 : cv34;
  mC[kSigZ2] = cv[5];
  //
  float ptI2 = ptI * ptI;
  float tgl2 = GetTgl() * GetTgl();
  if (special == 1) {
    mC[kSigSnpY] = cv[6] * ptI;
    mC[kSigSnpZ] = -sgcheck * cv[8] * r * ptI;
    mC[kSigSnp2] = fabs(cv[9] * r * r * ptI2);
    mC[kSigTglY] = (cv[10] * GetTgl() - sgcheck * cv[15]) * ptI / r;
    mC[kSigTglZ] = (cv[17] - sgcheck * cv[12] * GetTgl()) * ptI;
    mC[kSigTglSnp] = (-sgcheck * cv[18] + cv[13] * GetTgl()) * r * ptI2;
    mC[kSigTgl2] = fabs(cv[20] - 2 * sgcheck * cv[19] * mC[4] + cv[14] * tgl2) * ptI2;
    mC[kSigQ2PtY] = cv[10] * ptI2 / r * charge;
    mC[kSigQ2PtZ] = -sgcheck * cv[12] * ptI2 * charge;
    mC[kSigQ2PtSnp] = cv[13] * r * ptI * ptI2 * charge;
    mC[kSigQ2PtTgl] = (-sgcheck * cv[19] + cv[14] * GetTgl()) * r * ptI2 * ptI;
    mC[kSigQ2Pt2] = fabs(cv[14] * ptI2 * ptI2);
  } else if (special == 2) {
    mC[kSigSnpY] = -cv[10] * ptI * cs / sn;
    mC[kSigSnpZ] = cv[12] * cs * ptI;
    mC[kSigSnp2] = fabs(cv[14] * cs * cs * ptI2);
    mC[kSigTglY] = (sgcheck * cv[6] * GetTgl() - cv[15]) * ptI / sn;
    mC[kSigTglZ] = (cv[17] - sgcheck * cv[8] * GetTgl()) * ptI;
    mC[kSigTglSnp] = (cv[19] - sgcheck * cv[13] * GetTgl()) * cs * ptI2;
    mC[kSigTgl2] = fabs(cv[20] - 2 * sgcheck * cv[18] * GetTgl() + cv[9] * tgl2) * ptI2;
    mC[kSigQ2PtY] = sgcheck * cv[6] * ptI2 / sn * charge;
    mC[kSigQ2PtZ] = -sgcheck * cv[8] * ptI2 * charge;
    mC[kSigQ2PtSnp] = -sgcheck * cv[13] * cs * ptI * ptI2 * charge;
    mC[kSigQ2PtTgl] = (-sgcheck * cv[18] + cv[9] * mP[kTgl]) * ptI2 * ptI * charge;
    mC[kSigQ2Pt2] = fabs(cv[9] * ptI2 * ptI2);
  } else {
    double m00 = -sn; // m10=cs;
    double m23 = -pt * (sn + GetSnp() * cs / r), m43 = -pt * pt * (r * cs - GetSnp() * sn);
    double m24 = pt * (cs - GetSnp() * sn / r), m44 = -pt * pt * (r * sn + GetSnp() * cs);
    double m35 = pt, m45 = -pt * pt * GetTgl();
    //
    m43 *= charge;
    m44 *= charge;
    m45 *= charge;
    //
    double a1 = cv[13] - cv[9] * (m23 * m44 + m43 * m24) / m23 / m43;
    double a2 = m23 * m24 - m23 * (m23 * m44 + m43 * m24) / m43;
    double a3 = m43 * m44 - m43 * (m23 * m44 + m43 * m24) / m23;
    double a4 = cv[14] + 2. * cv[9];
    double a5 = m24 * m24 - 2. * m24 * m44 * m23 / m43;
    double a6 = m44 * m44 - 2. * m24 * m44 * m43 / m23;
    //
    mC[kSigSnpY] = (cv[10] * m43 - cv[6] * m44) / (m24 * m43 - m23 * m44) / m00;
    mC[kSigQ2PtY] = (cv[6] / m00 - mC[kSigSnpY] * m23) / m43;
    mC[kSigTglY] = (cv[15] / m00 - mC[kSigQ2PtY] * m45) / m35;
    mC[kSigSnpZ] = (cv[12] * m43 - cv[8] * m44) / (m24 * m43 - m23 * m44);
    mC[kSigQ2PtZ] = (cv[8] - mC[kSigSnpZ] * m23) / m43;
    mC[kSigTglZ] = cv[17] / m35 - mC[kSigQ2PtZ] * m45 / m35;
    mC[kSigSnp2] = fabs((a4 * a3 - a6 * a1) / (a5 * a3 - a6 * a2));
    mC[kSigQ2Pt2] = fabs((a1 - a2 * mC[kSigSnp2]) / a3);
    mC[kSigQ2PtSnp] = (cv[9] - mC[kSigSnp2] * m23 * m23 - mC[kSigQ2Pt2] * m43 * m43) / m23 / m43;
    double b1 = cv[18] - mC[kSigQ2PtSnp] * m23 * m45 - mC[kSigQ2Pt2] * m43 * m45;
    double b2 = m23 * m35;
    double b3 = m43 * m35;
    double b4 = cv[19] - mC[kSigQ2PtSnp] * m24 * m45 - mC[kSigQ2Pt2] * m44 * m45;
    double b5 = m24 * m35;
    double b6 = m44 * m35;
    mC[kSigTglSnp] = (b4 - b6 * b1 / b3) / (b5 - b6 * b2 / b3);
    mC[kSigQ2PtTgl] = b1 / b3 - b2 * mC[kSigTglSnp] / b3;
    mC[kSigTgl2] = fabs((cv[20] - mC[kSigQ2Pt2] * (m45 * m45) - mC[kSigQ2PtTgl] * 2. * m35 * m45) / (m35 * m35));
  }
  CheckCovariance();
}

//____________________________________________________________
bool TrackParCov::PropagateTo(float xk, const array<float, 3>& b)
{
  //----------------------------------------------------------------
  // Extrapolate this track to the plane X=xk in the field b[].
  //
  // X [cm] is in the "tracking coordinate system" of this track.
  // b[]={Bx,By,Bz} [kG] is in the Global coordidate system.
  //----------------------------------------------------------------

  float dx = xk - GetX();
  if (fabs(dx) < kAlmost0) {
    return true;
  }
  // Do not propagate tracks outside the ALICE detector
  if (fabs(dx) > 1e5 || fabs(GetY()) > 1e5 || fabs(GetZ()) > 1e5) {
    printf("Anomalous track, target X:%f\n", xk);
    //    Print();
    return false;
  }
  float crv = (fabs(b[2]) < kAlmost0) ? 0.f : GetCurvature(b[2]);
  float x2r = crv * dx;
  float f1 = GetSnp(), f2 = f1 + x2r;
  if ((fabs(f1) > kAlmost1) || (fabs(f2) > kAlmost1) || (fabs(GetQ2Pt()) < kAlmost0)) {
    return false;
  }
  float r1 = sqrtf((1.f - f1) * (1.f + f1));
  if (fabs(r1) < kAlmost0) {
    return false;
  }
  float r2 = sqrtf((1.f - f2) * (1.f + f2));
  if (fabs(r2) < kAlmost0) {
    return false;
  }

  float dy2dx = (f1 + f2) / (r1 + r2);
  float step = (fabs(x2r) < 0.05f) ? dx * fabs(r2 + f2 * dy2dx)                                       // chord
                                   : 2.f * asinf(0.5f * dx * sqrtf(1.f + dy2dx * dy2dx) * crv) / crv; // arc
  step *= sqrtf(1.f + GetTgl() * GetTgl());
  //
  // Get the track x,y,z,px/p,py/p,pz/p,p,sinAlpha,cosAlpha in the Global System
  array<float, 9> vecLab{ 0.f };
  if (!GetPosDirGlo(vecLab)) {
    return false;
  }
  //
  // matrix transformed with Bz component only
  float &c00 = mC[kSigY2], &c10 = mC[kSigZY], &c11 = mC[kSigZ2], &c20 = mC[kSigSnpY], &c21 = mC[kSigSnpZ],
        &c22 = mC[kSigSnp2], &c30 = mC[kSigTglY], &c31 = mC[kSigTglZ], &c32 = mC[kSigTglSnp], &c33 = mC[kSigTgl2],
        &c40 = mC[kSigQ2PtY], &c41 = mC[kSigQ2PtZ], &c42 = mC[kSigQ2PtSnp], &c43 = mC[kSigQ2PtTgl],
        &c44 = mC[kSigQ2Pt2];
  // evaluate matrix in double prec.
  double rinv = 1. / r1;
  double r3inv = rinv * rinv * rinv;
  double f24 = dx * b[2] * kB2C; // x2r/track[kQ2Pt];
  double f02 = dx * r3inv;
  double f04 = 0.5 * f24 * f02;
  double f12 = f02 * GetTgl() * f1;
  double f14 = 0.5 * f24 * f12; // 0.5*f24*f02*GetTgl()*f1;
  double f13 = dx * rinv;

  // b = C*ft
  double b00 = f02 * c20 + f04 * c40, b01 = f12 * c20 + f14 * c40 + f13 * c30;
  double b02 = f24 * c40;
  double b10 = f02 * c21 + f04 * c41, b11 = f12 * c21 + f14 * c41 + f13 * c31;
  double b12 = f24 * c41;
  double b20 = f02 * c22 + f04 * c42, b21 = f12 * c22 + f14 * c42 + f13 * c32;
  double b22 = f24 * c42;
  double b40 = f02 * c42 + f04 * c44, b41 = f12 * c42 + f14 * c44 + f13 * c43;
  double b42 = f24 * c44;
  double b30 = f02 * c32 + f04 * c43, b31 = f12 * c32 + f14 * c43 + f13 * c33;
  double b32 = f24 * c43;

  // a = f*b = f*C*ft
  double a00 = f02 * b20 + f04 * b40, a01 = f02 * b21 + f04 * b41, a02 = f02 * b22 + f04 * b42;
  double a11 = f12 * b21 + f14 * b41 + f13 * b31, a12 = f12 * b22 + f14 * b42 + f13 * b32;
  double a22 = f24 * b42;

  // F*C*Ft = C + (b + bt + a)
  c00 += b00 + b00 + a00;
  c10 += b10 + b01 + a01;
  c20 += b20 + b02 + a02;
  c30 += b30;
  c40 += b40;
  c11 += b11 + b11 + a11;
  c21 += b21 + b12 + a12;
  c31 += b31;
  c41 += b41;
  c22 += b22 + b22 + a22;
  c32 += b32;
  c42 += b42;

  CheckCovariance();

  // Rotate to the system where Bx=By=0.
  float bxy2 = b[0] * b[0] + b[1] * b[1];
  float bt = sqrtf(bxy2);
  float cosphi = 1.f, sinphi = 0.f;
  if (bt > kAlmost0) {
    cosphi = b[0] / bt;
    sinphi = b[1] / bt;
  }
  float bb = sqrtf(bxy2 + b[2] * b[2]);
  float costet = 1., sintet = 0.;
  if (bb > kAlmost0) {
    costet = b[2] / bb;
    sintet = bt / bb;
  }
  array<float, 7> vect{ costet * cosphi * vecLab[0] + costet * sinphi * vecLab[1] - sintet * vecLab[2],
                        -sinphi * vecLab[0] + cosphi * vecLab[1],
                        sintet * cosphi * vecLab[0] + sintet * sinphi * vecLab[1] + costet * vecLab[2],
                        costet * cosphi * vecLab[3] + costet * sinphi * vecLab[4] - sintet * vecLab[5],
                        -sinphi * vecLab[3] + cosphi * vecLab[4],
                        sintet * cosphi * vecLab[3] + sintet * sinphi * vecLab[4] + costet * vecLab[5],
                        vecLab[6] };

  // Do the helix step
  float sgn = GetSign();
  g3helx3(sgn * bb, step, vect);

  // Rotate back to the Global System
  vecLab[0] = cosphi * costet * vect[0] - sinphi * vect[1] + cosphi * sintet * vect[2];
  vecLab[1] = sinphi * costet * vect[0] + cosphi * vect[1] + sinphi * sintet * vect[2];
  vecLab[2] = -sintet * vect[0] + costet * vect[2];

  vecLab[3] = cosphi * costet * vect[3] - sinphi * vect[4] + cosphi * sintet * vect[5];
  vecLab[4] = sinphi * costet * vect[3] + cosphi * vect[4] + sinphi * sintet * vect[5];
  vecLab[5] = -sintet * vect[3] + costet * vect[5];

  // Rotate back to the Tracking System
  float sinalp = -vecLab[7], cosalp = vecLab[8];
  float t = cosalp * vecLab[0] - sinalp * vecLab[1];
  vecLab[1] = sinalp * vecLab[0] + cosalp * vecLab[1];
  vecLab[0] = t;
  t = cosalp * vecLab[3] - sinalp * vecLab[4];
  vecLab[4] = sinalp * vecLab[3] + cosalp * vecLab[4];
  vecLab[3] = t;

  // Do the final correcting step to the target plane (linear approximation)
  float x = vecLab[0], y = vecLab[1], z = vecLab[2];
  if (fabs(dx) > kAlmost0) {
    if (fabs(vecLab[3]) < kAlmost0) {
      return false;
    }
    dx = xk - vecLab[0];
    x += dx;
    y += vecLab[4] / vecLab[3] * dx;
    z += vecLab[5] / vecLab[3] * dx;
  }

  // Calculate the track parameters
  t = 1.f / sqrtf(vecLab[3] * vecLab[3] + vecLab[4] * vecLab[4]);
  mX = x;
  mP[kY] = y;
  mP[kZ] = z;
  mP[kSnp] = vecLab[4] * t;
  mP[kTgl] = vecLab[5] * t;
  mP[kQ2Pt] = sgn * t / vecLab[6];

  return true;
}

//______________________________________________
void TrackParCov::CheckCovariance()
{
  // This function forces the diagonal elements of the covariance matrix to be positive.
  // In case the diagonal element is bigger than the maximal allowed value, it is set to
  // the limit and the off-diagonal elements that correspond to it are set to zero.

  mC[kSigY2] = fabs(mC[kSigY2]);
  if (mC[kSigY2] > kCY2max) {
    float scl = sqrtf(kCY2max / mC[kSigY2]);
    mC[kSigY2] = kCY2max;
    mC[kSigZY] *= scl;
    mC[kSigSnpY] *= scl;
    mC[kSigTglY] *= scl;
    mC[kSigQ2PtY] *= scl;
  }
  mC[kSigZ2] = fabs(mC[kSigZ2]);
  if (mC[kSigZ2] > kCZ2max) {
    float scl = sqrtf(kCZ2max / mC[kSigZ2]);
    mC[kSigZ2] = kCZ2max;
    mC[kSigZY] *= scl;
    mC[kSigSnpZ] *= scl;
    mC[kSigTglZ] *= scl;
    mC[kSigQ2PtZ] *= scl;
  }
  mC[kSigSnp2] = fabs(mC[kSigSnp2]);
  if (mC[kSigSnp2] > kCSnp2max) {
    float scl = sqrtf(kCSnp2max / mC[kSigSnp2]);
    mC[kSigSnp2] = kCSnp2max;
    mC[kSigSnpY] *= scl;
    mC[kSigSnpZ] *= scl;
    mC[kSigTglSnp] *= scl;
    mC[kSigQ2PtSnp] *= scl;
  }
  mC[kSigTgl2] = fabs(mC[kSigTgl2]);
  if (mC[kSigTgl2] > kCTgl2max) {
    float scl = sqrtf(kCTgl2max / mC[kSigTgl2]);
    mC[kSigTgl2] = kCTgl2max;
    mC[kSigTglY] *= scl;
    mC[kSigTglZ] *= scl;
    mC[kSigTglSnp] *= scl;
    mC[kSigQ2PtTgl] *= scl;
  }
  mC[kSigQ2Pt2] = fabs(mC[kSigQ2Pt2]);
  if (mC[kSigQ2Pt2] > kC1Pt2max) {
    float scl = sqrtf(kC1Pt2max / mC[kSigQ2Pt2]);
    mC[kSigQ2Pt2] = kC1Pt2max;
    mC[kSigQ2PtY] *= scl;
    mC[kSigQ2PtZ] *= scl;
    mC[kSigQ2PtSnp] *= scl;
    mC[kSigQ2PtTgl] *= scl;
  }
}

//______________________________________________
void TrackParCov::ResetCovariance(float s2)
{
  // Reset the covarince matrix to "something big"
  double d0(kCY2max), d1(kCZ2max), d2(kCSnp2max), d3(kCTgl2max), d4(kC1Pt2max);
  if (s2 > kAlmost0) {
    d0 = GetSigmaY2() * s2;
    d1 = GetSigmaZ2() * s2;
    d2 = GetSigmaSnp2() * s2;
    d3 = GetSigmaTgl2() * s2;
    d4 = GetSigma1Pt2() * s2;
    if (d0 > kCY2max) {
      d0 = kCY2max;
    }
    if (d1 > kCZ2max) {
      d1 = kCZ2max;
    }
    if (d2 > kCSnp2max) {
      d2 = kCSnp2max;
    }
    if (d3 > kCTgl2max) {
      d3 = kCTgl2max;
    }
    if (d4 > kC1Pt2max) {
      d4 = kC1Pt2max;
    }
  }
  memset(mC, 0, kCovMatSize * sizeof(float));
  mC[kSigY2] = d0;
  mC[kSigZ2] = d1;
  mC[kSigSnp2] = d2;
  mC[kSigTgl2] = d3;
  mC[kSigQ2Pt2] = d4;
}

//______________________________________________
float TrackParCov::GetPredictedChi2(const array<float, 2>& p, const array<float, 3>& cov) const
{
  // Estimate the chi2 of the space point "p" with the cov. matrix "cov"
  float sdd = GetSigmaY2() + cov[0];
  float sdz = GetSigmaZY() + cov[1];
  float szz = GetSigmaZ2() + cov[2];
  float det = sdd * szz - sdz * sdz;

  if (fabs(det) < kAlmost0) {
    return kVeryBig;
  }

  float d = GetY() - p[0];
  float z = GetZ() - p[1];

  return (d * (szz * d - sdz * z) + z * (sdd * z - d * sdz)) / det;
}

bool TrackParCov::Update(const array<float, 2>& p, const array<float, 3>& cov)
{
  // Update the track parameters with the space point "p" having
  // the covariance matrix "cov"

  float &cm00 = mC[kSigY2], &cm10 = mC[kSigZY], &cm11 = mC[kSigZ2], &cm20 = mC[kSigSnpY], &cm21 = mC[kSigSnpZ],
        &cm22 = mC[kSigSnp2], &cm30 = mC[kSigTglY], &cm31 = mC[kSigTglZ], &cm32 = mC[kSigTglSnp], &cm33 = mC[kSigTgl2],
        &cm40 = mC[kSigQ2PtY], &cm41 = mC[kSigQ2PtZ], &cm42 = mC[kSigQ2PtSnp], &cm43 = mC[kSigQ2PtTgl],
        &cm44 = mC[kSigQ2Pt2];

  // use double precision?
  double r00 = cov[0] + cm00, r01 = cov[1] + cm10, r11 = cov[2] + cm11;
  double det = r00 * r11 - r01 * r01;

  if (fabs(det) < kAlmost0) {
    return false;
  }
  double detI = 1. / det;
  double tmp = r00;
  r00 = r11 * detI;
  r11 = tmp * detI;
  r01 = -r01 * detI;

  double k00 = cm00 * r00 + cm10 * r01, k01 = cm00 * r01 + cm10 * r11;
  double k10 = cm10 * r00 + cm11 * r01, k11 = cm10 * r01 + cm11 * r11;
  double k20 = cm20 * r00 + cm21 * r01, k21 = cm20 * r01 + cm21 * r11;
  double k30 = cm30 * r00 + cm31 * r01, k31 = cm30 * r01 + cm31 * r11;
  double k40 = cm40 * r00 + cm41 * r01, k41 = cm40 * r01 + cm41 * r11;

  double dy = p[kY] - GetY(), dz = p[kZ] - GetZ();
  double sf = GetSnp() + k20 * dy + k21 * dz;
  if (fabs(sf) > kAlmost1) {
    return false;
  }

  mP[kY] += k00 * dy + k01 * dz;
  mP[kZ] += k10 * dy + k11 * dz;
  mP[kSnp] = sf;
  mP[kTgl] += k30 * dy + k31 * dz;
  mP[kQ2Pt] += k40 * dy + k41 * dz;

  double c01 = cm10, c02 = cm20, c03 = cm30, c04 = cm40;
  double c12 = cm21, c13 = cm31, c14 = cm41;

  cm00 -= k00 * cm00 + k01 * cm10;
  cm10 -= k00 * c01 + k01 * cm11;
  cm20 -= k00 * c02 + k01 * c12;
  cm30 -= k00 * c03 + k01 * c13;
  cm40 -= k00 * c04 + k01 * c14;

  cm11 -= k10 * c01 + k11 * cm11;
  cm21 -= k10 * c02 + k11 * c12;
  cm31 -= k10 * c03 + k11 * c13;
  cm41 -= k10 * c04 + k11 * c14;

  cm22 -= k20 * c02 + k21 * c12;
  cm32 -= k20 * c03 + k21 * c13;
  cm42 -= k20 * c04 + k21 * c14;

  cm33 -= k30 * c03 + k31 * c13;
  cm43 -= k30 * c04 + k31 * c14;

  cm44 -= k40 * c04 + k41 * c14;

  CheckCovariance();

  return true;
}

//______________________________________________
bool TrackParCov::CorrectForMaterial(float x2x0, float xrho, float mass, bool anglecorr, float dedx)
{
  //------------------------------------------------------------------
  // This function corrects the track parameters for the crossed material.
  // "x2x0"   - X/X0, the thickness in units of the radiation length.
  // "xrho" - is the product length*density (g/cm^2).
  //     It should be passed as negative when propagating tracks
  //     from the intreaction point to the outside of the central barrel.
  // "mass" - the mass of this particle (GeV/c^2). Negative mass means charge=2 particle
  // "dedx" - mean enery loss (GeV/(g/cm^2), if <=kCalcdEdxAuto : calculate on the fly
  // "anglecorr" - switch for the angular correction
  //------------------------------------------------------------------
  constexpr float kMSConst2 = 0.0136f * 0.0136f;
  constexpr float kMaxELossFrac = 0.3f; // max allowed fractional eloss
  constexpr float kMinP = 0.01f;        // kill below this momentum
  float& fP2 = mP[kSnp];
  float& fP3 = mP[kTgl];
  float& fP4 = mP[kQ2Pt];

  float& fC22 = mC[kSigSnp2];
  float& fC33 = mC[kSigTgl2];
  float& fC43 = mC[kSigQ2PtTgl];
  float& fC44 = mC[kSigQ2Pt2];
  //
  float csp2 = (1.f - fP2) * (1.f + fP2); // cos(phi)^2
  float cst2I = (1.f + fP3 * fP3);        // 1/cos(lambda)^2
  // Apply angle correction, if requested
  if (anglecorr) {
    float angle = sqrtf(cst2I / (csp2));
    x2x0 *= angle;
    xrho *= angle;
  }

  float p = GetP();
  if (mass < 0) {
    p += p; // q=2 particle
  }
  float p2 = p * p, mass2 = mass * mass;
  float e2 = p2 + mass2;
  float beta2 = p2 / e2;

  // Calculating the multiple scattering corrections******************
  float cC22(0.f), cC33(0.f), cC43(0.f), cC44(0.f);
  if (x2x0 != 0.f) {
    float theta2 = kMSConst2 / (beta2 * p2) * fabs(x2x0);
    if (mass < 0) {
      theta2 *= 4.f; // q=2 particle
    }
    if (theta2 > kPI * kPI) {
      return false;
    }
    float fp34 = fP3 * fP4;
    float t2c2I = theta2 * cst2I;
    cC22 = t2c2I * csp2;
    cC33 = t2c2I * cst2I;
    cC43 = t2c2I * fp34;
    cC44 = theta2 * fp34 * fp34;
    // optimes this
    //    cC22 = theta2*((1.-fP2)*(1.+fP2))*(1. + fP3*fP3);
    //    cC33 = theta2*(1. + fP3*fP3)*(1. + fP3*fP3);
    //    cC43 = theta2*fP3*fP4*(1. + fP3*fP3);
    //    cC44 = theta2*fP3*fP4*fP3*fP4;
  }

  // Calculating the energy loss corrections************************
  float cP4 = 1.f;
  if ((xrho != 0.f) && (beta2 < 1.f)) {
    if (dedx < kCalcdEdxAuto + kAlmost1) { // request to calculate dedx on the fly
      dedx = BetheBlochSolid(p / fabs(mass));
      if (mass < 0) {
        dedx *= 4.f; // z=2 particle
      }
    }

    float dE = dedx * xrho;
    float e = sqrtf(e2);
    if (fabs(dE) > kMaxELossFrac * e) {
      return false; // 30% energy loss is too much!
    }
    float eupd = e + dE;
    float pupd2 = eupd * eupd - mass2;
    if (pupd2 < kMinP * kMinP) {
      return false;
    }
    cP4 = p / sqrtf(pupd2);
    //
    // Approximate energy loss fluctuation (M.Ivanov)
    constexpr float knst = 0.07f; // To be tuned.
    float sigmadE = knst * sqrtf(fabs(dE)) * e / p2 * fP4;
    cC44 += sigmadE * sigmadE;
  }

  // Applying the corrections*****************************
  fC22 += cC22;
  fC33 += cC33;
  fC43 += cC43;
  fC44 += cC44;
  fP4 *= cP4;

  CheckCovariance();

  return true;
}

//______________________________________________________________
void TrackParCov::Print() const
{
  // print parameters
  PrintParam();
  printf(
    "%7s %+.3e\n"
    "%7s %+.3e %+.3e\n"
    "%7s %+.3e %+.3e %+.3e\n"
    "%7s %+.3e %+.3e %+.3e %+.3e\n"
    "%7s %+.3e %+.3e %+.3e %+.3e %+.3e\n",
    "CovMat:", mC[kSigY2], "", mC[kSigZY], mC[kSigZ2], "", mC[kSigSnpY], mC[kSigSnpZ], mC[kSigSnp2], "", mC[kSigTglY],
    mC[kSigTglZ], mC[kSigTglSnp], mC[kSigTgl2], "", mC[kSigQ2PtY], mC[kSigQ2PtZ], mC[kSigQ2PtSnp], mC[kSigQ2PtTgl],
    mC[kSigQ2Pt2]);
}

//=================================================
//
// Aux. methods for tracks manipulation
//
//=================================================

void o2::Base::Track::g3helx3(float qfield, float step, array<float, 7>& vect)
{
  /******************************************************************
   *                                                                *
   *       GEANT3 tracking routine in a constant field oriented     *
   *       along axis 3                                             *
   *       Tracking is performed with a conventional                *
   *       helix step method                                        *
   *                                                                *
   *       Authors    R.Brun, M.Hansroul  *********                 *
   *       Rewritten  V.Perevoztchikov                              *
   *                                                                *
   *       Rewritten in C++ by I.Belikov                            *
   *                                                                *
   *  qfield (kG)       - particle charge times magnetic field      *
   *  step   (cm)       - step length along the helix               *
   *  vect[7](cm,GeV/c) - input/output x, y, z, px/p, py/p ,pz/p, p *
   *                                                                *
   ******************************************************************/
  const int ix = 0, iy = 1, iz = 2, ipx = 3, ipy = 4, ipz = 5, ipp = 6;
  constexpr float kOvSqSix = 0.408248f; // sqrtf(1./6.);

  float cosx = vect[ipx], cosy = vect[ipy], cosz = vect[ipz];

  float rho = qfield * kB2C / vect[ipp];
  float tet = rho * step;

  float tsint, sintt, sint, cos1t;
  if (fabs(tet) > 0.03f) {
    sint = sinf(tet);
    sintt = sint / tet;
    tsint = (tet - sint) / tet;
    float t = sinf(0.5f * tet);
    cos1t = 2 * t * t / tet;
  } else {
    tsint = tet * tet / 6.f;
    sintt = (1.f - tet * kOvSqSix) * (1.f + tet * kOvSqSix); // 1.- tsint;
    sint = tet * sintt;
    cos1t = 0.5f * tet;
  }

  float f1 = step * sintt;
  float f2 = step * cos1t;
  float f3 = step * tsint * cosz;
  float f4 = -tet * cos1t;
  float f5 = sint;

  vect[ix] += f1 * cosx - f2 * cosy;
  vect[iy] += f1 * cosy + f2 * cosx;
  vect[iz] += f1 * cosz + f3;

  vect[ipx] += f4 * cosx - f5 * cosy;
  vect[ipy] += f4 * cosy + f5 * cosx;
}

//____________________________________________________
float o2::Base::Track::BetheBlochSolid(float bg, float rho, float kp1, float kp2, float meanI, float meanZA)
{
  //
  // This is the parameterization of the Bethe-Bloch formula inspired by Geant.
  //
  // bg  - beta*gamma
  // rho - density [g/cm^3]
  // kp1 - density effect first junction point
  // kp2 - density effect second junction point
  // meanI - mean excitation energy [GeV]
  // meanZA - mean Z/A
  //
  // The default values for the kp* parameters are for silicon.
  // The returned value is in [GeV/(g/cm^2)].
  //
  constexpr float mK = 0.307075e-3f; // [GeV*cm^2/g]
  constexpr float me = 0.511e-3f;    // [GeV/c^2]
  kp1 *= 2.303f;
  kp2 *= 2.303f;
  float bg2 = bg * bg;
  float maxT = 2.f * me * bg2; // neglecting the electron mass

  //*** Density effect
  float d2 = 0.;
  const float x = log(bg);
  const float lhwI = log(28.816 * 1e-9 * sqrtf(rho * meanZA) / meanI);
  if (x > kp2) {
    d2 = lhwI + x - 0.5;
  } else if (x > kp1) {
    double r = (kp2 - x) / (kp2 - kp1);
    d2 = lhwI + x - 0.5 + (0.5 - lhwI - kp1) * r * r * r;
  }
  return mK * meanZA * (1 + bg2) / bg2 * (0.5 * log(2 * me * bg2 * maxT / (meanI * meanI)) - bg2 / (1 + bg2) - d2);
}
