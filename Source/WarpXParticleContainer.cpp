
#include <limits>

#include <ParticleContainer.H>
#include <WarpXParticleContainer.H>
#include <AMReX_AmrParGDB.H>
#include <WarpX_f.H>
#include <WarpX.H>

using namespace amrex;

int WarpXParticleContainer::do_not_push = 0;

WarpXParIter::WarpXParIter (ContainerType& pc, int level)
    : ParIter(pc, level, MFItInfo().SetDynamic(WarpX::do_dynamic_scheduling))
{
}

#if (AMREX_SPACEDIM == 2)
void
WarpXParIter::GetPosition (Cuda::DeviceVector<Real>& x, Cuda::DeviceVector<Real>& y, Cuda::DeviceVector<Real>& z) const
{
    amrex::ParIter<0,0,PIdx::nattribs>::GetPosition(x, z);
    y.resize(x.size(), std::numeric_limits<Real>::quiet_NaN());
}

void
WarpXParIter::SetPosition (const Cuda::DeviceVector<Real>& x, const Cuda::DeviceVector<Real>& y, const Cuda::DeviceVector<Real>& z)
{
    amrex::ParIter<0,0,PIdx::nattribs>::SetPosition(x, z);
}
#endif

WarpXParticleContainer::WarpXParticleContainer (AmrCore* amr_core, int ispecies)
    : ParticleContainer<0,0,PIdx::nattribs>(amr_core->GetParGDB())
    , species_id(ispecies)
{
    for (unsigned int i = PIdx::Ex; i <= PIdx::Bz; ++i) {
        communicate_real_comp[i] = false; // Don't need to communicate E and B.
    }
    SetParticleSize();
    ReadParameters();

    // Initialize temporary local arrays for charge/current deposition
    int num_threads = 1;
    #ifdef _OPENMP
    #pragma omp parallel
    #pragma omp single
    num_threads = omp_get_num_threads();
    #endif
    local_rho.resize(num_threads);
    local_jx.resize(num_threads);
    local_jy.resize(num_threads);
    local_jz.resize(num_threads);
    m_xp.resize(num_threads);
    m_yp.resize(num_threads);
    m_zp.resize(num_threads);
    m_giv.resize(num_threads);
    for (int i = 0; i < num_threads; ++i)
      {
        local_rho[i].reset(nullptr);
        local_jx[i].reset(nullptr);
        local_jy[i].reset(nullptr);
        local_jz[i].reset(nullptr);
      }

}

void
WarpXParticleContainer::ReadParameters ()
{
    static bool initialized = false;
    if (!initialized)
    {
	ParmParse pp("particles");

#ifdef AMREX_USE_GPU
        do_tiling = false; // By default, tiling is off on GPU
#else
        do_tiling = true;
#endif
	pp.query("do_tiling",  do_tiling);
        pp.query("do_not_push", do_not_push);

	initialized = true;
    }
}

void
WarpXParticleContainer::AllocData ()
{
    // have to resize here, not in the constructor because grids have not
    // been built when constructor was called.
    reserveData();
    resizeData();
}

void
WarpXParticleContainer::AddOneParticle (int lev, int grid, int tile,
                                        Real x, Real y, Real z,
                                        const std::array<Real,PIdx::nattribs>& attribs)
{
    auto& particle_tile = GetParticles(lev)[std::make_pair(grid,tile)];
    AddOneParticle(particle_tile, x, y, z, attribs);
}

void
WarpXParticleContainer::AddOneParticle (ParticleTileType& particle_tile,
                                        Real x, Real y, Real z,
                                        const std::array<Real,PIdx::nattribs>& attribs)
{
    ParticleType p;
    p.id()  = ParticleType::NextID();
    p.cpu() = ParallelDescriptor::MyProc();
#if (AMREX_SPACEDIM == 3)
    p.pos(0) = x;
    p.pos(1) = y;
    p.pos(2) = z;
#elif (AMREX_SPACEDIM == 2)
    p.pos(0) = x;
    p.pos(1) = z;
#endif

    particle_tile.push_back(p);
    particle_tile.push_back_real(attribs);
}

void
WarpXParticleContainer::AddNParticles (int lev,
                                       int n, const Real* x, const Real* y, const Real* z,
				       const Real* vx, const Real* vy, const Real* vz,
				       int nattr, const Real* attr, int uniqueparticles)
{
    BL_ASSERT(nattr == 1);
    const Real* weight = attr;

    int ibegin, iend;
    if (uniqueparticles) {
	ibegin = 0;
	iend = n;
    } else {
	int myproc = ParallelDescriptor::MyProc();
	int nprocs = ParallelDescriptor::NProcs();
	int navg = n/nprocs;
	int nleft = n - navg * nprocs;
	if (myproc < nleft) {
	    ibegin = myproc*(navg+1);
	    iend = ibegin + navg+1;
	} else {
	    ibegin = myproc*navg + nleft;
	    iend = ibegin + navg;
	}
    }

    //  Add to grid 0 and tile 0
    // Redistribute() will move them to proper places.
    std::pair<int,int> key {0,0};
    auto& particle_tile = GetParticles(lev)[key];

    for (int i = ibegin; i < iend; ++i)
    {
        ParticleType p;
        p.id()  = ParticleType::NextID();
        p.cpu() = ParallelDescriptor::MyProc();
#if (AMREX_SPACEDIM == 3)
        p.pos(0) = x[i];
        p.pos(1) = y[i];
        p.pos(2) = z[i];
#elif (AMREX_SPACEDIM == 2)
        p.pos(0) = x[i];
        p.pos(1) = z[i];
#endif
        particle_tile.push_back(p);
    }

    std::size_t np = iend-ibegin;

    if (np > 0)
    {
        particle_tile.push_back_real(PIdx::w , weight + ibegin, weight + iend);
        particle_tile.push_back_real(PIdx::ux,     vx + ibegin,     vx + iend);
        particle_tile.push_back_real(PIdx::uy,     vy + ibegin,     vy + iend);
        particle_tile.push_back_real(PIdx::uz,     vz + ibegin,     vz + iend);

        for (int comp = PIdx::uz+1; comp < PIdx::nattribs; ++comp)
        {
            particle_tile.push_back_real(comp, np, 0.0);
        }
    }

    Redistribute();
}


void
WarpXParticleContainer::DepositCurrent(WarpXParIter& pti,
                                          RealVector& wp, RealVector& uxp,
                                          RealVector& uyp, RealVector& uzp,
                                          MultiFab& jx, MultiFab& jy, MultiFab& jz,
                                          MultiFab* cjx, MultiFab* cjy, MultiFab* cjz,
                                          const long np_current, const long np,
                                          int thread_num, int lev, Real dt )
{
  Real *jx_ptr, *jy_ptr, *jz_ptr;
  const std::array<Real,3>& xyzmin_tile = WarpX::LowerCorner(pti.tilebox(), lev);
  const std::array<Real,3>& dx = WarpX::CellSize(lev);
  const std::array<Real,3>& cdx = WarpX::CellSize(std::max(lev-1,0));
  const std::array<Real, 3>& xyzmin = xyzmin_tile;
  const long lvect = 8;

  BL_PROFILE_VAR_NS("PICSAR::CurrentDeposition", blp_pxr_cd);
  BL_PROFILE_VAR_NS("PPC::Evolve::Accumulate", blp_accumulate);

  Box tbx = convert(pti.tilebox(), WarpX::jx_nodal_flag);
  Box tby = convert(pti.tilebox(), WarpX::jy_nodal_flag);
  Box tbz = convert(pti.tilebox(), WarpX::jz_nodal_flag);

  // WarpX assumes the same number of guard cells for Jx, Jy, Jz
  long ngJ = jx.nGrow();

  // Deposit charge for particles that are not in the current buffers
  if (np_current > 0)
    {
      tbx.grow(ngJ);
      tby.grow(ngJ);
      tbz.grow(ngJ);

      local_jx[thread_num]->resize(tbx);
      local_jy[thread_num]->resize(tby);
      local_jz[thread_num]->resize(tbz);

      jx_ptr = local_jx[thread_num]->dataPtr();
      jy_ptr = local_jy[thread_num]->dataPtr();
      jz_ptr = local_jz[thread_num]->dataPtr();

      FArrayBox* local_jx_ptr = local_jx[thread_num].get();
      AMREX_LAUNCH_HOST_DEVICE_LAMBDA(tbx, b,
      {
        local_jx_ptr->setVal(0.0, b, 0, 1);
      });

      FArrayBox* local_jy_ptr = local_jy[thread_num].get();
      AMREX_LAUNCH_HOST_DEVICE_LAMBDA(tby, b,
      {
        local_jy_ptr->setVal(0.0, b, 0, 1);
      });

      FArrayBox* local_jz_ptr = local_jz[thread_num].get();
      AMREX_LAUNCH_HOST_DEVICE_LAMBDA(tbz, b,
      {
        local_jz_ptr->setVal(0.0, b, 0, 1);
      });

      auto jxntot = local_jx[thread_num]->length();
      auto jyntot = local_jy[thread_num]->length();
      auto jzntot = local_jz[thread_num]->length();

      BL_PROFILE_VAR_START(blp_pxr_cd);
      warpx_current_deposition(
                               jx_ptr, &ngJ, jxntot.getVect(),
                               jy_ptr, &ngJ, jyntot.getVect(),
                               jz_ptr, &ngJ, jzntot.getVect(),
                               &np_current,
                               m_xp[thread_num].dataPtr(),
                               m_yp[thread_num].dataPtr(),
                               m_zp[thread_num].dataPtr(),
                               uxp.dataPtr(), uyp.dataPtr(), uzp.dataPtr(),
                               m_giv[thread_num].dataPtr(),
                               wp.dataPtr(), &this->charge,
                               &xyzmin[0], &xyzmin[1], &xyzmin[2],
                               &dt, &dx[0], &dx[1], &dx[2],
                               &WarpX::nox,&WarpX::noy,&WarpX::noz,
                               &lvect,&WarpX::current_deposition_algo);
      BL_PROFILE_VAR_STOP(blp_pxr_cd);

      BL_PROFILE_VAR_START(blp_accumulate);

      FArrayBox const* local_jx_const_ptr = local_jx[thread_num].get();
      FArrayBox* global_jx_ptr = jx.fabPtr(pti);
      AMREX_LAUNCH_HOST_DEVICE_LAMBDA(tbx, thread_bx,
      {
        global_jx_ptr->atomicAdd(*local_jx_const_ptr, thread_bx, thread_bx, 0, 0, 1);
      });

      FArrayBox const* local_jy_const_ptr = local_jy[thread_num].get();
      FArrayBox* global_jy_ptr = jy.fabPtr(pti);
      AMREX_LAUNCH_HOST_DEVICE_LAMBDA(tby, thread_bx,
      {
        global_jy_ptr->atomicAdd(*local_jy_const_ptr, thread_bx, thread_bx, 0, 0, 1);
      });

      FArrayBox const* local_jz_const_ptr = local_jz[thread_num].get();
      FArrayBox* global_jz_ptr = jz.fabPtr(pti);
      AMREX_LAUNCH_HOST_DEVICE_LAMBDA(tbz, thread_bx,
      {
        global_jz_ptr->atomicAdd(*local_jz_const_ptr, thread_bx, thread_bx, 0, 0, 1);
      });

      BL_PROFILE_VAR_STOP(blp_accumulate);
    }

  // Deposit charge for particles that are in the current buffers
  if (np_current < np)
    {
      const IntVect& ref_ratio = WarpX::RefRatio(lev-1);
      const Box& ctilebox = amrex::coarsen(pti.tilebox(),ref_ratio);
      const std::array<Real,3>& cxyzmin_tile = WarpX::LowerCorner(ctilebox, lev-1);

      tbx = amrex::convert(ctilebox, WarpX::jx_nodal_flag);
      tby = amrex::convert(ctilebox, WarpX::jy_nodal_flag);
      tbz = amrex::convert(ctilebox, WarpX::jz_nodal_flag);
      tbx.grow(ngJ);
      tby.grow(ngJ);
      tbz.grow(ngJ);

      local_jx[thread_num]->resize(tbx);
      local_jy[thread_num]->resize(tby);
      local_jz[thread_num]->resize(tbz);

      jx_ptr = local_jx[thread_num]->dataPtr();
      jy_ptr = local_jy[thread_num]->dataPtr();
      jz_ptr = local_jz[thread_num]->dataPtr();

      FArrayBox* local_jx_ptr = local_jx[thread_num].get();
      AMREX_LAUNCH_HOST_DEVICE_LAMBDA(tbx, b,
      {
        local_jx_ptr->setVal(0.0, b, 0, 1);
      });

      FArrayBox* local_jy_ptr = local_jy[thread_num].get();
      AMREX_LAUNCH_HOST_DEVICE_LAMBDA(tby, b,
      {
        local_jy_ptr->setVal(0.0, b, 0, 1);
      });

      FArrayBox* local_jz_ptr = local_jz[thread_num].get();
      AMREX_LAUNCH_HOST_DEVICE_LAMBDA(tbz, b,
      {
        local_jz_ptr->setVal(0.0, b, 0, 1);
      });
      auto jxntot = local_jx[thread_num]->length();
      auto jyntot = local_jy[thread_num]->length();
      auto jzntot = local_jz[thread_num]->length();

      long ncrse = np - np_current;
      BL_PROFILE_VAR_START(blp_pxr_cd);
      warpx_current_deposition(
                               jx_ptr, &ngJ, jxntot.getVect(),
                               jy_ptr, &ngJ, jyntot.getVect(),
                               jz_ptr, &ngJ, jzntot.getVect(),
                               &ncrse,
                               m_xp[thread_num].dataPtr() +np_current,
                               m_yp[thread_num].dataPtr() +np_current,
                               m_zp[thread_num].dataPtr() +np_current,
                               uxp.dataPtr()+np_current,
                               uyp.dataPtr()+np_current,
                               uzp.dataPtr()+np_current,
                               m_giv[thread_num].dataPtr()+np_current,
                               wp.dataPtr()+np_current, &this->charge,
                               &cxyzmin_tile[0], &cxyzmin_tile[1], &cxyzmin_tile[2],
                               &dt, &cdx[0], &cdx[1], &cdx[2],
                               &WarpX::nox,&WarpX::noy,&WarpX::noz,
                               &lvect,&WarpX::current_deposition_algo);
      BL_PROFILE_VAR_STOP(blp_pxr_cd);

      BL_PROFILE_VAR_START(blp_accumulate);

      FArrayBox const* local_jx_const_ptr = local_jx[thread_num].get();
      FArrayBox* global_jx_ptr = cjx->fabPtr(pti);
      AMREX_LAUNCH_HOST_DEVICE_LAMBDA(tbx, thread_bx,
      {
        global_jx_ptr->atomicAdd(*local_jx_const_ptr, thread_bx, thread_bx, 0, 0, 1);
      });

      FArrayBox const* local_jy_const_ptr = local_jy[thread_num].get();
      FArrayBox* global_jy_ptr = cjy->fabPtr(pti);
      AMREX_LAUNCH_HOST_DEVICE_LAMBDA(tby, thread_bx,
      {
        global_jy_ptr->atomicAdd(*local_jy_const_ptr, thread_bx, thread_bx, 0, 0, 1);
      });

      FArrayBox const* local_jz_const_ptr = local_jz[thread_num].get();
      FArrayBox* global_jz_ptr = cjz->fabPtr(pti);
      AMREX_LAUNCH_HOST_DEVICE_LAMBDA(tbz, thread_bx,
      {
        global_jz_ptr->atomicAdd(*local_jz_const_ptr, thread_bx, thread_bx, 0, 0, 1);
      });

      BL_PROFILE_VAR_STOP(blp_accumulate);
    }
};


void
WarpXParticleContainer::DepositCharge ( WarpXParIter& pti, RealVector& wp,
                                  MultiFab* rhomf, MultiFab* crhomf, int icomp,
                                 const long np_current,
                                 const long np, int thread_num, int lev )
{

  BL_PROFILE_VAR_NS("PICSAR::ChargeDeposition", blp_pxr_chd);
  BL_PROFILE_VAR_NS("PPC::Evolve::Accumulate", blp_accumulate);

  const std::array<Real,3>& xyzmin_tile = WarpX::LowerCorner(pti.tilebox(), lev);
  const long lvect = 8;

  long ngRho = rhomf->nGrow();
  Real* data_ptr;
  Box tile_box = convert(pti.tilebox(), IntVect::TheUnitVector());

  const std::array<Real,3>& dx = WarpX::CellSize(lev);
  const std::array<Real,3>& cdx = WarpX::CellSize(std::max(lev-1,0));

  // Deposit charge for particles that are not in the current buffers
  if (np_current > 0)
    {
      const std::array<Real, 3>& xyzmin = xyzmin_tile;
      tile_box.grow(ngRho);
      local_rho[thread_num]->resize(tile_box);
      FArrayBox* local_rho_ptr = local_rho[thread_num].get();
      AMREX_LAUNCH_HOST_DEVICE_LAMBDA(tile_box, b,
      {
        local_rho_ptr->setVal(0.0, b, 0, 1);
      });

      data_ptr = local_rho[thread_num]->dataPtr();
      auto rholen = local_rho[thread_num]->length();
#if (AMREX_SPACEDIM == 3)
      const long nx = rholen[0]-1-2*ngRho;
      const long ny = rholen[1]-1-2*ngRho;
      const long nz = rholen[2]-1-2*ngRho;
#else
      const long nx = rholen[0]-1-2*ngRho;
      const long ny = 0;
      const long nz = rholen[1]-1-2*ngRho;
#endif
      BL_PROFILE_VAR_START(blp_pxr_chd);
      warpx_charge_deposition(data_ptr, &np_current,
                              m_xp[thread_num].dataPtr(),
                              m_yp[thread_num].dataPtr(),
                              m_zp[thread_num].dataPtr(),
                              wp.dataPtr(),
                              &this->charge,
                              &xyzmin[0], &xyzmin[1], &xyzmin[2],
                              &dx[0], &dx[1], &dx[2], &nx, &ny, &nz,
                              &ngRho, &ngRho, &ngRho,
                              &WarpX::nox,&WarpX::noy,&WarpX::noz,
                              &lvect, &WarpX::charge_deposition_algo);
      BL_PROFILE_VAR_STOP(blp_pxr_chd);

      const int ncomp = 1;
      FArrayBox const* local_fab = local_rho[thread_num].get();
      FArrayBox*       global_fab = rhomf->fabPtr(pti);
      BL_PROFILE_VAR_START(blp_accumulate);
      AMREX_LAUNCH_HOST_DEVICE_LAMBDA(tile_box, tbx,
      {
        global_fab->atomicAdd(*local_fab, tbx, tbx, 0, icomp, ncomp);
      });
      BL_PROFILE_VAR_STOP(blp_accumulate);
    }

  // Deposit charge for particles that are in the current buffers
  if (np_current < np)
    {
      const IntVect& ref_ratio = WarpX::RefRatio(lev-1);
      const Box& ctilebox = amrex::coarsen(pti.tilebox(), ref_ratio);
      const std::array<Real,3>& cxyzmin_tile = WarpX::LowerCorner(ctilebox, lev-1);

      tile_box = amrex::convert(ctilebox, IntVect::TheUnitVector());
      tile_box.grow(ngRho);
      local_rho[thread_num]->resize(tile_box);
      FArrayBox* local_rho_ptr = local_rho[thread_num].get();
      AMREX_LAUNCH_HOST_DEVICE_LAMBDA(tile_box, b,
      {
        local_rho_ptr->setVal(0.0, b, 0, 1);
      });

      data_ptr = local_rho[thread_num]->dataPtr();
      auto rholen = local_rho[thread_num]->length();
#if (AMREX_SPACEDIM == 3)
      const long nx = rholen[0]-1-2*ngRho;
      const long ny = rholen[1]-1-2*ngRho;
      const long nz = rholen[2]-1-2*ngRho;
#else
      const long nx = rholen[0]-1-2*ngRho;
      const long ny = 0;
      const long nz = rholen[1]-1-2*ngRho;
#endif

      long ncrse = np - np_current;
      BL_PROFILE_VAR_START(blp_pxr_chd);
      warpx_charge_deposition(data_ptr, &ncrse,
                              m_xp[thread_num].dataPtr() + np_current,
                              m_yp[thread_num].dataPtr() + np_current,
                              m_zp[thread_num].dataPtr() + np_current,
                              wp.dataPtr() + np_current,
                              &this->charge,
                              &cxyzmin_tile[0], &cxyzmin_tile[1], &cxyzmin_tile[2],
                              &cdx[0], &cdx[1], &cdx[2], &nx, &ny, &nz,
                              &ngRho, &ngRho, &ngRho,
                              &WarpX::nox,&WarpX::noy,&WarpX::noz,
                              &lvect, &WarpX::charge_deposition_algo);
      BL_PROFILE_VAR_STOP(blp_pxr_chd);

      const int ncomp = 1;
      FArrayBox const* local_fab = local_rho[thread_num].get();
      FArrayBox*       global_fab = crhomf->fabPtr(pti);
      BL_PROFILE_VAR_START(blp_accumulate);
      AMREX_LAUNCH_HOST_DEVICE_LAMBDA(tile_box, tbx,
      {
        global_fab->atomicAdd(*local_fab, tbx, tbx, 0, icomp, ncomp);
      });
      BL_PROFILE_VAR_STOP(blp_accumulate);
    }
};

void
WarpXParticleContainer::DepositCharge (Vector<std::unique_ptr<MultiFab> >& rho, bool local)
{

    int num_levels = rho.size();
    int finest_level = num_levels - 1;

    // each level deposits it's own particles
    const int ng = rho[0]->nGrow();
    for (int lev = 0; lev < num_levels; ++lev) {

        rho[lev]->setVal(0.0, ng);

        const auto& gm = m_gdb->Geom(lev);
        const auto& ba = m_gdb->ParticleBoxArray(lev);
        const auto& dm = m_gdb->DistributionMap(lev);

        const Real* dx  = gm.CellSize();
        const Real* plo = gm.ProbLo();
        BoxArray nba = ba;
        nba.surroundingNodes();

        for (WarpXParIter pti(*this, lev); pti.isValid(); ++pti) {
            const Box& box = nba[pti];

            auto& wp = pti.GetAttribs(PIdx::w);
            const auto& particles = pti.GetArrayOfStructs();
            int nstride = particles.dataShape().first;
            const long np  = pti.numParticles();

            FArrayBox& rhofab = (*rho[lev])[pti];

            WRPX_DEPOSIT_CIC(particles.dataPtr(), nstride, np,
                             wp.dataPtr(), &this->charge,
                             rhofab.dataPtr(), box.loVect(), box.hiVect(),
                             plo, dx, &ng);
        }

        if (!local) rho[lev]->SumBoundary(gm.periodicity());
    }

    // now we average down fine to crse
    std::unique_ptr<MultiFab> crse;
    for (int lev = finest_level - 1; lev >= 0; --lev) {
        const BoxArray& fine_BA = rho[lev+1]->boxArray();
        const DistributionMapping& fine_dm = rho[lev+1]->DistributionMap();
        BoxArray coarsened_fine_BA = fine_BA;
        coarsened_fine_BA.coarsen(m_gdb->refRatio(lev));

        MultiFab coarsened_fine_data(coarsened_fine_BA, fine_dm, 1, 0);
        coarsened_fine_data.setVal(0.0);

        IntVect ratio(AMREX_D_DECL(2, 2, 2));  // FIXME

        for (MFIter mfi(coarsened_fine_data); mfi.isValid(); ++mfi) {
            const Box& bx = mfi.validbox();
            const Box& crse_box = coarsened_fine_data[mfi].box();
            const Box& fine_box = (*rho[lev+1])[mfi].box();
            WRPX_SUM_FINE_TO_CRSE_NODAL(bx.loVect(), bx.hiVect(), ratio.getVect(),
                                        coarsened_fine_data[mfi].dataPtr(), crse_box.loVect(), crse_box.hiVect(),
                                        (*rho[lev+1])[mfi].dataPtr(), fine_box.loVect(), fine_box.hiVect());
        }

        rho[lev]->copy(coarsened_fine_data, m_gdb->Geom(lev).periodicity(), FabArrayBase::ADD);
    }
}

std::unique_ptr<MultiFab>
WarpXParticleContainer::GetChargeDensity (int lev, bool local)
{
    const auto& gm = m_gdb->Geom(lev);
    const auto& ba = m_gdb->ParticleBoxArray(lev);
    const auto& dm = m_gdb->DistributionMap(lev);
    BoxArray nba = ba;
    nba.surroundingNodes();

    const std::array<Real,3>& dx = WarpX::CellSize(lev);

    const int ng = WarpX::nox;

    auto rho = std::unique_ptr<MultiFab>(new MultiFab(nba,dm,1,ng));
    rho->setVal(0.0);

#ifdef _OPENMP
#pragma omp parallel
#endif
    {
        Cuda::DeviceVector<Real> xp, yp, zp;
        FArrayBox local_rho;

        for (WarpXParIter pti(*this, lev); pti.isValid(); ++pti)
        {
            const Box& box = pti.validbox();

            auto& wp = pti.GetAttribs(PIdx::w);

            const long np  = pti.numParticles();

            pti.GetPosition(xp, yp, zp);

            const std::array<Real,3>& xyzmin_tile = WarpX::LowerCorner(pti.tilebox(), lev);
            const std::array<Real,3>& xyzmin_grid = WarpX::LowerCorner(box, lev);

            // Data on the grid
            Real* data_ptr;
            FArrayBox& rhofab = (*rho)[pti];
#ifdef _OPENMP
            Box tile_box = convert(pti.tilebox(), IntVect::TheUnitVector());
            const std::array<Real, 3>& xyzmin = xyzmin_tile;
            tile_box.grow(ng);
            local_rho.resize(tile_box);
            local_rho = 0.0;
            data_ptr = local_rho.dataPtr();
            auto rholen = local_rho.length();
#else
            const std::array<Real, 3>& xyzmin = xyzmin_grid;
            data_ptr = rhofab.dataPtr();
            auto rholen = rhofab.length();
#endif

#if (AMREX_SPACEDIM == 3)
            const long nx = rholen[0]-1-2*ng;
            const long ny = rholen[1]-1-2*ng;
            const long nz = rholen[2]-1-2*ng;
#else
            const long nx = rholen[0]-1-2*ng;
            const long ny = 0;
            const long nz = rholen[1]-1-2*ng;
#endif

            long nxg = ng;
            long nyg = ng;
            long nzg = ng;
            long lvect = 8;

            warpx_charge_deposition(data_ptr,
                                    &np,
                                    xp.dataPtr(),
                                    yp.dataPtr(),
                                    zp.dataPtr(), wp.dataPtr(),
                                    &this->charge, &xyzmin[0], &xyzmin[1], &xyzmin[2],
                                    &dx[0], &dx[1], &dx[2], &nx, &ny, &nz,
                                    &nxg, &nyg, &nzg, &WarpX::nox,&WarpX::noy,&WarpX::noz,
                                    &lvect, &WarpX::charge_deposition_algo);

#ifdef _OPENMP
            rhofab.atomicAdd(local_rho);
#endif
        }

    }

    if (!local) rho->SumBoundary(gm.periodicity());

    return rho;
}

Real WarpXParticleContainer::sumParticleCharge(bool local) {

    amrex::Real total_charge = 0.0;

    for (int lev = 0; lev < finestLevel(); ++lev)
    {

#ifdef _OPENMP
#pragma omp parallel reduction(+:total_charge)
#endif
        for (WarpXParIter pti(*this, lev); pti.isValid(); ++pti)
        {
            auto& wp = pti.GetAttribs(PIdx::w);
            for (unsigned long i = 0; i < wp.size(); i++) {
                total_charge += wp[i];
            }
        }
    }

    if (!local) ParallelDescriptor::ReduceRealSum(total_charge);
    total_charge *= this->charge;
    return total_charge;
}

std::array<Real, 3> WarpXParticleContainer::meanParticleVelocity(bool local) {

    amrex::Real vx_total = 0.0;
    amrex::Real vy_total = 0.0;
    amrex::Real vz_total = 0.0;

    long np_total = 0;

    amrex::Real inv_clight_sq = 1.0/PhysConst::c/PhysConst::c;

    for (int lev = 0; lev <= finestLevel(); ++lev) {

#ifdef _OPENMP
#pragma omp parallel reduction(+:vx_total, vy_total, vz_total, np_total)
#endif
        for (WarpXParIter pti(*this, lev); pti.isValid(); ++pti)
        {
            auto& ux = pti.GetAttribs(PIdx::ux);
            auto& uy = pti.GetAttribs(PIdx::uy);
            auto& uz = pti.GetAttribs(PIdx::uz);

            np_total += pti.numParticles();

            for (unsigned long i = 0; i < ux.size(); i++) {
                Real usq = (ux[i]*ux[i] + uy[i]*uy[i] + uz[i]*uz[i])*inv_clight_sq;
                Real gaminv = 1.0/std::sqrt(1.0 + usq);
                vx_total += ux[i]*gaminv;
                vy_total += uy[i]*gaminv;
                vz_total += uz[i]*gaminv;
            }
        }
    }

    if (!local) {
        ParallelDescriptor::ReduceRealSum(vx_total);
        ParallelDescriptor::ReduceRealSum(vy_total);
        ParallelDescriptor::ReduceRealSum(vz_total);
        ParallelDescriptor::ReduceLongSum(np_total);
    }

    std::array<Real, 3> mean_v;
    if (np_total > 0) {
        mean_v[0] = vx_total / np_total;
        mean_v[1] = vy_total / np_total;
        mean_v[2] = vz_total / np_total;
    }

    return mean_v;
}

Real WarpXParticleContainer::maxParticleVelocity(bool local) {

    amrex::Real max_v = 0.0;

    for (int lev = 0; lev <= finestLevel(); ++lev)
    {

#ifdef _OPENMP
#pragma omp parallel reduction(max:max_v)
#endif
        for (WarpXParIter pti(*this, lev); pti.isValid(); ++pti)
        {
            auto& ux = pti.GetAttribs(PIdx::ux);
            auto& uy = pti.GetAttribs(PIdx::uy);
            auto& uz = pti.GetAttribs(PIdx::uz);
            for (unsigned long i = 0; i < ux.size(); i++) {
                max_v = std::max(max_v, sqrt(ux[i]*ux[i] + uy[i]*uy[i] + uz[i]*uz[i]));
            }
        }
    }

    if (!local) ParallelDescriptor::ReduceRealMax(max_v);
    return max_v;
}

void
WarpXParticleContainer::PushXES (Real dt)
{
    BL_PROFILE("WPC::PushXES()");

    int num_levels = finestLevel() + 1;

    for (int lev = 0; lev < num_levels; ++lev) {
        const auto& gm = m_gdb->Geom(lev);
        const RealBox& prob_domain = gm.ProbDomain();
        for (WarpXParIter pti(*this, lev); pti.isValid(); ++pti) {
            auto& particles = pti.GetArrayOfStructs();
            int nstride = particles.dataShape().first;
            const long np  = pti.numParticles();

            auto& attribs = pti.GetAttribs();
            auto& uxp = attribs[PIdx::ux];
            auto& uyp = attribs[PIdx::uy];
            auto& uzp = attribs[PIdx::uz];

            WRPX_PUSH_LEAPFROG_POSITIONS(particles.dataPtr(), nstride, np,
                                         uxp.dataPtr(), uyp.dataPtr(),
#if AMREX_SPACEDIM == 3
                                         uzp.dataPtr(),
#endif
                                         &dt,
                                         prob_domain.lo(), prob_domain.hi());
        }
    }
}

void
WarpXParticleContainer::PushX (Real dt)
{
    for (int lev = 0; lev <= finestLevel(); ++lev) {
        PushX(lev, dt);
    }
}

void
WarpXParticleContainer::PushX (int lev, Real dt)
{
    BL_PROFILE("WPC::PushX()");
    BL_PROFILE_VAR_NS("WPC::PushX::Copy", blp_copy);
    BL_PROFILE_VAR_NS("WPC:PushX::Push", blp_pxr_pp);

    if (do_not_push) return;

    MultiFab* cost = WarpX::getCosts(lev);

#ifdef _OPENMP
#pragma omp parallel
#endif
    {
        Cuda::DeviceVector<Real> xp, yp, zp, giv;

        for (WarpXParIter pti(*this, lev); pti.isValid(); ++pti)
        {
            Real wt = amrex::second();

            auto& attribs = pti.GetAttribs();

            auto& uxp = attribs[PIdx::ux];
            auto& uyp = attribs[PIdx::uy];
            auto& uzp = attribs[PIdx::uz];

            const long np = pti.numParticles();

            giv.resize(np);

            //
            // copy data from particle container to temp arrays
            //
            BL_PROFILE_VAR_START(blp_copy);
            pti.GetPosition(xp, yp, zp);
            BL_PROFILE_VAR_STOP(blp_copy);

            //
            // Particle Push
            //
            BL_PROFILE_VAR_START(blp_pxr_pp);
            warpx_particle_pusher_positions(&np,
                                            xp.dataPtr(),
                                            yp.dataPtr(),
                                            zp.dataPtr(),
                                            uxp.dataPtr(), uyp.dataPtr(), uzp.dataPtr(),
                                            giv.dataPtr(), &dt);
            BL_PROFILE_VAR_STOP(blp_pxr_pp);

            //
            // copy particle data back
            //
            BL_PROFILE_VAR_START(blp_copy);
            pti.SetPosition(xp, yp, zp);
            BL_PROFILE_VAR_STOP(blp_copy);

            if (cost) {
                const Box& tbx = pti.tilebox();
                wt = (amrex::second() - wt) / tbx.d_numPts();
                FArrayBox* costfab = cost->fabPtr(pti);
                AMREX_LAUNCH_HOST_DEVICE_LAMBDA ( tbx, work_box,
                {
                    costfab->plus(wt, work_box);
                });
            }
        }
    }
}
