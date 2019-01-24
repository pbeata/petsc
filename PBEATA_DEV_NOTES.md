#### Development Notes

The purpose of this forked version of the PETSc repository is to add reproducibility features to the library. We are completing this work as part of a larger reproducibility study investigating said features of floating-point applications in scientific computing such as in Finite Element Analysis.

The following list of changes were introduced to the library:
- Use compensated summation in the MatSetValues routine for MPI-based applications
- (more will come in the future..)

Paul A. Beata
Postdoctoral Researcher
North Carolina State University
