
module warpx_boosted_frame_module

  use iso_c_binding
  use amrex_fort_module, only : amrex_real
  use constants, only : clight

  implicit none

contains

!
! Given cell-centered data in the boosted reference frame of the simulation, 
! this transforms E and B in place so that the multifab now contains values
! in the lab frame. This routine assumes that the simulation frame is moving
! in the positive z direction with respect to the lab frame.
! 
  subroutine warpx_lorentz_transform_z(data, dlo, dhi, tlo, thi, gamma_boost, beta_boost) &
       bind(C, name="warpx_lorentz_transform_z")

    integer(c_int),   intent(in)    :: dlo(3), dhi(3)
    integer(c_int),   intent(in)    :: tlo(3), thi(3)
    real(amrex_real), intent(inout) :: data(dlo(1):dhi(1),dlo(2):dhi(2),dlo(3):dhi(3), 10)
    real(amrex_real), intent(in)    :: gamma_boost, beta_boost

    integer i, j, k
    real(amrex_real) e_lab, b_lab, j_lab, r_lab
    
    do k = tlo(3), thi(3)
       do j = tlo(2), thi(2)
          do i = tlo(1), thi(1)
             
             ! Transform the transverse E and B fields. Note that ez and bz are not 
             ! changed by the tranform.
             e_lab = gamma_boost * (data(i, j, k, 1) + beta_boost*clight*data(i, j, k, 5))
             b_lab = gamma_boost * (data(i, j, k, 5) + beta_boost*data(i, j, k, 1)/clight)

             data(i, j, k, 1) = e_lab
             data(i, j, k, 5) = b_lab

             e_lab = gamma_boost * (data(i, j, k, 2) - beta_boost*clight*data(i, j, k, 4))
             b_lab = gamma_boost * (data(i, j, k, 4) - beta_boost*data(i, j, k, 2)/clight)

             data(i, j, k, 2) = e_lab
             data(i, j, k, 4) = b_lab

             ! Transform the charge and current density. Only the z component of j is affected.
             j_lab = gamma_boost*(data(i, j, k, 9) + beta_boost*clight*data(i, j, k, 10))
             r_lab = gamma_boost*(data(i, j, k, 10) + beta_boost*data(i, j, k, 9)/clight)

             data(i, j, k, 9)  = j_lab
             data(i, j, k, 10) = r_lab

          end do
       end do
    end do

  end subroutine warpx_lorentz_transform_z

  subroutine warpx_copy_slice_3d(lo, hi, tmp, tlo, thi, &
                              buf, blo, bhi, ncomp, &
                              i_boost, i_lab) &
       bind(C, name="warpx_copy_slice_3d")

    integer       ,   intent(in)    :: ncomp, i_boost, i_lab
    integer       ,   intent(in)    :: lo(3), hi(3)
    integer       ,   intent(in)    :: tlo(3), thi(3)
    integer       ,   intent(in)    :: blo(3), bhi(3)
    real(amrex_real), intent(inout) :: tmp(tlo(1):thi(1),tlo(2):thi(2),tlo(3):thi(3),ncomp)
    real(amrex_real), intent(inout) :: buf(blo(1):bhi(1),blo(2):bhi(2),blo(3):bhi(3),ncomp)
    
    integer n, i, j, k
    
    do n = 1, ncomp
       do j = lo(2), hi(2)
          do i = lo(1), hi(1)
             buf(i, j, i_lab, n) = tmp(i, j, i_boost, n)
          end do
       end do
    end do
    
  end subroutine warpx_copy_slice_3d

  subroutine warpx_copy_slice_2d(lo, hi, tmp, tlo, thi, &
                                 buf, blo, bhi, ncomp, &
                                 i_boost, i_lab) &
       bind(C, name="warpx_copy_slice_2d")

    integer       ,   intent(in)    :: ncomp, i_boost, i_lab
    integer       ,   intent(in)    :: lo(2),   hi(2)
    integer       ,   intent(in)    :: tlo(2), thi(2)
    integer       ,   intent(in)    :: blo(2), bhi(2)
    real(amrex_real), intent(inout) :: tmp(tlo(1):thi(1),tlo(2):thi(2),ncomp)
    real(amrex_real), intent(inout) :: buf(blo(1):bhi(1),blo(2):bhi(2),ncomp)
    
    integer n, i, j, k
    
    do n = 1, ncomp
       do i = lo(1), hi(1)
          buf(i, i_lab, n) = tmp(i, i_boost, n)
       end do
    end do
    
  end subroutine warpx_copy_slice_2d

end module warpx_boosted_frame_module
