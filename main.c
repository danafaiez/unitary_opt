   
#include <string.h>
#include <stdlib.h>
#include <complex.h>
#include <assert.h>
#include <gsl/gsl_multimin.h>
#include "defs.h"
#include "io.h"
#include "fock.h"
#include "ham.h"
#include "data_structures.h"
#include "basis_states.h"
#include "funcs.h"
#include "clapack.h"
#include "locate_multivs.h"
#include "ent_entropy.h"
#include "coarse_grain.h"
#include "thermo.h"
#include "energy_graining.h"
#include "min.h"
#include "monte_carlo.h"
#include "max_region_prob.h"
#include "unitary.h"
void compare_pms(PARAMS * p1, PARAMS * p2)
{
   if (p1->num_sites != p2->num_sites)
      printf("p1->num_sites != p2->num_sites\n");
   if (p1->num_particles != p2->num_particles)
      printf("p1->num_particles != p2->num_particles\n");
   if (p1->L != p2->L)
      printf("p1->L != p2->L\n");
   if (p1->t[0] != p2->t[0])
      printf("p1->t[0] != p2->t[0]\n");
   if (p1->t[1] != p2->t[1])
      printf("p1->t[1] != p2->t[1]\n");
   if (p1->U != p2->U)
      printf("p1->U != p2->U\n");
   if (p1->Up != p2->Up)
      printf("p1->Up != p2->Up\n");
   if (p1->Linit != p2->Linit)
      printf("p1->Linit != p2->Linit\n");
}

double * abs_carr(double * psi, int numstates) 
{
   double * a;
   newarr_(a,numstates/2);
   int i;
   for(i=0; i < numstates/2; i++)
   {
      a[i] = sqrt(SQR(psi[2*i]) + SQR(psi[2*i+1]));
   }
   return a;
}

double * compute_psi_current(PARAMS * pm, HTABLE * hash, ull * states, _Complex double * psi)
{
   int i;
   int sep = 1;
   double * curr=0;
   newarr_(curr,pm->L);

   {
      int j;  
      ull state = states[i];
      _Complex double psi1 = psi[i];
      for(j=sep;j < pm->L;j++)
      {
         if (bit_val(state,j))
         {
            if (bit_val(state,j-sep))
               continue;
            flip_bit(state,j-sep);
            flip_bit(state,j);
            int * indx = _htable_get(hash,&state);
            _Complex double psi2 = psi[*indx];
            flip_bit(state,j-sep);
            flip_bit(state,j);

            //curr[j] += cimag((psi1+psi2)*conj(psi2-psi1));
            curr[j] += cimag(psi1*conj(psi2)-psi2*conj(psi1));
         }
      }
   }
   return curr;
}

double compute_psi_corr(PARAMS * pm, HTABLE * hash, ull * states, _Complex double * psi)
{
   int i;
   double corr=0;
   for(i=0;i < pm->numstates;i++)
   {
      int j;  
      ull state = states[i];
      _Complex double psi1 = psi[i];
      for(j=1;j < pm->L;j++)
      {
         if (bit_val(state,j))
         {
            if (bit_val(state,j-1))
               continue;
            flip_bit(state,j-1);
            flip_bit(state,j);
            int * indx = _htable_get(hash,&state);
            _Complex double psi2 = psi[*indx];
            flip_bit(state,j-1);
            flip_bit(state,j);

            _Complex double delta = psi2-psi1;
            double dr = creal(delta);
            double di = cimag(delta);
            //corr += dr*dr+di*di;
            corr += creal(psi2*conj(psi1));
         }
      }
   }
   return corr;
}


void   write_small_psis(PARAMS * pm, double * density, double  cutoff,char * filename, ull * states)
{
   int i;
   FILE * file = fopen(filename,"w");

   for(i=0;i < size_(density);i++)
   {
      if (density[i] < cutoff)
      {
         print_binary_(file,(int) states[i], pm->L); 
         fprintf(file,"\n");
      }
   }
   fclose(file);
}

double * density(PARAMS * pm, _Complex double * psi, ull * states)
{
   int i;
   double * rho;
   newarr_(rho,pm->L);

   for(i=0;i < pm->numstates;i++)
   {
      int j;
      double re=creal(psi[i]);
      double im=cimag(psi[i]);
      double prob = re*re+im*im;
      for(j=0; j < pm->L; j++)
      {
         if  (bit_val(states[i],j))
            rho[j] += prob;
      }   
   }
   return rho;
}


#ifndef UNITARY_TEST

int main(int argc, char * argv[])
{
   int i;
   int Lx=0;
   int Ly=0;
   int N= 0; //Define the no. of points on the lattice      
   int Ne=0;  //Define the no. of spinless electrons
   int evnumber = 0;
   int x_init;
   int x_fin;  
  
   double cutoff=0;
   double beta=-1;//If read in an positive, start in an intial thermal state
   double t[2];
   double V=1.0;
   long long int numstates;
   int calc_eigenvectors = 0;
   int compute_psi_correlation =0;
   int single_state_entropy = 0;
   int evolve_system = 0;
   int minimize_S_EX; 
   int minimize_Sent;
   int minimize_foe;
   int minimize_unitary=0;
   int minimize = 0;
   int monte_carlo = 0;
   int monte_ent;
   int monte_Sxe;
   int monte_density;

   int maximize_reg_prob = 0;
   int calc_entropy = 0;
   int calc_multi = 0;
   int calc_reduced_evs = 0; 
   int calc_obs_xe=0;
   int calc_FOE=0;
   int print_dens=0;
   int superpose=0;
   int start = 0;
   int end = 0;
   int num_in_ensemble=0;
   int size_of_box;
   int numCoarseEs;
   unsigned int seed = 1000;
   double time=0;
   double t_shift=0;
   double delta_t=0;
   double delta_beta=0.01;
   char * evector_in_name;
   char * energy_in_name;
   char * dirname;
   int step_size;
   int iter;
   double beta_carlo;
   PARAMS * pm;
   calloc_(pm,1);


   for (i = 1; i < argc; )
   {
      if (argv[i][0] == '#')
      {
         i++;
      }
      else if (strstr (argv[i], "help"))
      {
         printf("e.g.: tv -num_part 3 -L 3  -t 1.1 -tp 0.1 -U 0.5 -Up 0.3 -calc_eigenvectors \n");
         exit(2);
      }
      else if (strstr (argv[i], "-num") && strstr (argv[i], "sites"))
      {
         if (strstr (argv[i], "bath"))
         {
            if (sscanf(argv[i+1],"%d", &pm->num_bath_sites) != 1) 
            {
               printf("num_bath_sites messed up\n");
               exit(2); 
            }
         }
         else if (sscanf(argv[i+1],"%d", &pm->num_sites) != 1) //shouldn't be used because it's specidied by Lx and Ly
         {
            printf("num_sites messed up\n");
            exit(2); 
         }
         i += 2;
      }
      else if (strstr (argv[i], "-num") && strstr (argv[i], "part"))
      {
         if (sscanf(argv[i+1],"%d", &pm->num_particles) != 1) 
         {
            printf("num_particles messed up\n");
            exit(2); 
         }
         i += 2;
      }
      else if (strstr (argv[i], "-monte") && strstr (argv[i], "moves"))
      {
         //= number of phases changed in one step
         if (sscanf(argv[i+1],"%d", &pm->monte_moves) != 1) 
         {
            printf("monte_moves messed up\n");
            exit(2); 
         }
         i += 2;
      }
      else if (strstr (argv[i], "-seed"))
      {
         if (sscanf(argv[i+1],"%d", &seed) != 1) 
         {
            printf("seed messed up\n");
            exit(2); 
         }
         srandom(seed);
         i += 2;
      }
      else if (strstr (argv[i], "-start"))
      {
         if (sscanf(argv[i+1],"%d", &start) != 1) 
         {
            printf("start messed up\n");
            exit(2); 
         }
         i += 2;
      }
      else if (strstr (argv[i], "-end"))
      {
         if (sscanf(argv[i+1],"%d", &end) != 1) 
         {
            printf("start messed up\n");
            exit(2); 
         }
         i += 2;
      }
      else if (strstr (argv[i], "-superpos"))
      {
         superpose = 1;
         i++;
      }
      else if (strstr (argv[i], "-sing") && strstr(argv[i],"sta") && strstr(argv[i],"entr"))
      {
         single_state_entropy = 1;
         i++;
      }
      else if (strstr (argv[i], "-max") &&  strstr (argv[i], "bath") && strstr (argv[i], "sites"))
      {
         if (sscanf(argv[i+1],"%d", &pm->max_bath_sites) != 1) 
         {
            printf("max_bath_sites messed up\n");
            exit(2); 
         }
         i += 2;
      }
      else if (strstr (argv[i], "-num") && (strstr (argv[i], "ens") || (strstr (argv[i], "comp")) ))
      {
         if (sscanf(argv[i+1],"%d", &num_in_ensemble) != 1) 
         {
            printf("num_in_ensemble messed up\n");
            exit(2); 
         }
         i += 2;
      }
      else if (strstr (argv[i], "-num") && strstr (argv[i], "evs"))
      {
         if (sscanf(argv[i+1],"%d", &pm->num_evs) != 1) 
         {
            printf("num_evs messed up\n");
            exit(2); 
         }
         i += 2;
      }
      else if (strstr (argv[i], "-L"))
      {
         if (strstr (argv[i], "init"))
         {
            if (sscanf(argv[i+1],"%d", &pm->Linit) != 1) 
            {
               printf("Linit  messed up\n");
               exit(2); 
            }
         }
         else if (sscanf(argv[i+1],"%d", &pm->L) != 1) 
         {
            printf("L  messed up\n");
            exit(2); 
         }
         i += 2;
      }
     

  else if (strstr (argv[i], "-minimize")  &&  strstr (argv[i], "foe"))
       {
          if (sscanf(argv[i+1],"%d", &pm->minimize_foe) != 1)
          {
              printf("minimize_foe  messed up\n");
              exit(2);
          }
          i += 2;
       }

  
   else if (strstr (argv[i], "-minimize")  &&  strstr (argv[i], "EX"))
      {
         if (sscanf(argv[i+1],"%d", &pm->minimize_S_EX) != 1)
         {
             printf("minimize_S_EX  messed up\n");
             exit(2);
         }
         i += 2;
      }
  
   else if (strstr (argv[i], "-monte")  &&  strstr (argv[i], "ent"))
      {
         if (sscanf(argv[i+1],"%d", &pm->monte_ent) != 1)
         {
             printf("monte_ent  messed up\n");
             exit(2);
         }
         i += 2;
      }
   
   else if (strstr (argv[i], "-monte")  &&  strstr (argv[i], "density"))
      {
         if (sscanf(argv[i+1],"%d", &pm->monte_density) != 1)
         {
             printf("monte_density  messed up\n");
             exit(2);
         }
         i += 2;
      }
   else if (strstr (argv[i], "-monte")  &&  strstr (argv[i], "Sxe"))
      {
         if (sscanf(argv[i+1],"%d", &pm->monte_Sxe) != 1)
         {
             printf("monte_Sxe  messed up\n");
             exit(2);
         }
         i += 2;
      }

   else if (strstr (argv[i], "-minimize")  &&  strstr (argv[i], "ent"))
      {
         if (sscanf(argv[i+1],"%d", &pm->minimize_Sent) != 1)
         {
             printf("minimize_Sent  messed up\n");
             exit(2);
         }
         i += 2;
      }

   else if (strstr (argv[i], "-minimize")  &&  strstr (argv[i], "unitary"))
      {
         minimize_unitary = 1;
         i++;
      }
        else if (strstr (argv[i], "-evolve") &&  strstr (argv[i], "sys"))
      {
         evolve_system = 1; 
         i++;
      }
        else if (strstr (argv[i], "-monte") &&  strstr (argv[i], "carlo"))
      {
         monte_carlo = 1; 
         i++;
      }
      
      else if (strstr (argv[i], "-maximize") &&  strstr (argv[i], "_reg"))
      {
         maximize_reg_prob = 1;
         i++;
      }

      else if (strstr (argv[i], "-minimize"))
     {
          minimize = 1;
         i++;
     } 


      else if (strstr (argv[i], "-comp") &&  strstr (argv[i], "psi")&&  strstr (argv[i], "corr"))
      {
         compute_psi_correlation = 1; 
         i++;
      }
      else if (strstr (argv[i], "-calc") &&  strstr (argv[i], "eig") &&  strstr (argv[i], "vect"))
      {
         calc_eigenvectors = 1; 
         i++;
      }
      else if (strstr (argv[i], "-calc") &&  strstr (argv[i], "entro"))
      {
         calc_entropy = 1; 
         i++;
      }
      else if (strstr (argv[i], "-calc") &&  strstr (argv[i], "mult"))
      {
         calc_multi = 1; 
         i++;
      }
      else if (strstr (argv[i], "-calc") &&  strstr (argv[i], "red") && strstr(argv[i],"evs"))
      {
         calc_reduced_evs = 1; 
         i++;
      }
      else if (strstr (argv[i], "-calc") &&  strstr (argv[i], "obs") && strstr(argv[i],"xe"))
      {
         calc_obs_xe = 1; 
         i++;
      }
      else if (strstr (argv[i], "-calc") &&  strstr (argv[i], "foe"))
      {
         calc_FOE = 1; 
         i++;
      }
      else if (strstr (argv[i], "-prin") &&  strstr (argv[i], "dens"))
      {
         print_dens = 1; 
         i++;
      }
      else if (!strcmp (argv[i], "-t"))
      {
         if (sscanf(argv[i+1],"%lf", &pm->t[0]) != 1) 
         {
            printf("t  messed up\n");
            exit(2); 
         }
         i += 2;
      }
      else if (!strcmp (argv[i], "-beta"))
      {
         if (sscanf(argv[i+1],"%lf", &beta) != 1) 
         {
            printf("beta  messed up\n");
            exit(2); 
         }
         i += 2;
      }
      else if (!strcmp (argv[i], "-iter"))
      {
         if (sscanf(argv[i+1],"%d", &pm->iter) != 1) 
         {
            printf("iter  messed up\n");
            exit(2); 
         }
         i += 2;
      }

      else if (!strcmp (argv[i], "-tp"))
      {
         if (sscanf(argv[i+1],"%lf", &pm->t[1]) != 1) 
         {
            printf("tp  messed up\n");
            exit(2); 
         }
         i += 2;
      }
      else if (!strcmp (argv[i], "-U"))
      {
         if (sscanf(argv[i+1],"%lf", &pm->U) != 1) 
         {
            printf("U  messed up\n");
            exit(2); 
         }
         i += 2;
      }
      else if (!strcmp (argv[i], "-Up"))
      {
         if (sscanf(argv[i+1],"%lf", &pm->Up) != 1) 
         {
            printf("Up  messed up\n");
            exit(2); 
         }
         i += 2;
      }
      else if (!strcmp (argv[i], "-time"))
      {
         if (sscanf(argv[i+1],"%lf", &time) != 1) 
         {
            printf("time  messed up\n");
            exit(2); 
         }
         i += 2;
      }
      else if (strstr (argv[i], "-delt") && strstr (argv[i], "_t"))
      {
         if (sscanf(argv[i+1],"%lf", &delta_t) != 1) 
         {
            printf("delta_t  messed up\n");
            exit(2); 
         }
         i += 2;
      }
      else if (strstr (argv[i], "-t") && strstr (argv[i], "shift"))
      {
         if (sscanf(argv[i+1],"%lf", &t_shift) != 1) 
         {
            printf("t_shift messed up\n");
            exit(2); 
         }
         i += 2;
      }
      else if (strstr (argv[i], "-beta") && strstr (argv[i], "carlo"))
      {
         if (sscanf(argv[i+1],"%lf", &pm->beta_carlo) != 1) 
         {
            printf("beta_carlo messed up\n");
            exit(2); 
         }
         i += 2;
      }
      else if (strstr (argv[i], "-step") && strstr (argv[i], "size"))
      {
         if (sscanf(argv[i+1],"%lf", &pm->step_size) != 1) 
         {
            printf("step_size messed up\n");
            exit(2); 
         }
         i += 2;
      }

      else if (strstr (argv[i], "-x") && strstr (argv[i], "_init"))
      {
         if (sscanf(argv[i+1],"%d", &pm->x_init) != 1)
         {
            printf("x_init messed up\n");
            exit(2);
         }
         i += 2;
      }

      else if (strstr (argv[i], "-x") && strstr (argv[i], "_fin"))
      {
         if (sscanf(argv[i+1],"%d", &pm->x_fin) != 1)
         {
            printf("x_fin messed up\n");
            exit(2);
         }
         i += 2;
      }




      else if (strstr (argv[i], "-delt") && strstr (argv[i], "beta"))
      {
         if (sscanf(argv[i+1],"%lf", &delta_beta) != 1) 
         {
            printf("delta_beta  messed up\n");
            exit(2); 
         }
         i += 2;
      }

/*
      else if (strstr (argv[i], "-size") && strstr (argv[i], "box"))
       {
          if (sscanf(argv[i+1],"%d", &pm->size_of_box) != 1)
          {
             printf("size_of_box  messed up\n");
             exit(2);
          }
          i += 2;
       }
*/

      else if (strstr (argv[i], "-size") && strstr (argv[i], "box"))
      {
         if (sscanf(argv[i+1],"%d", &size_of_box) != 1) 
         {
            printf("size_of_box  messed up\n");
            exit(2); 
         }
         i += 2;
      }

      else if (strstr (argv[i], "-num") && strstr (argv[i], "Coar") && strstr (argv[i], "E"))
      {
         if (sscanf(argv[i+1],"%d", &numCoarseEs) != 1) 
         {
            printf("numCoarseEs  messed up\n");
            exit(2); 
         }
         i += 2;
      }



      else if (strstr (argv[i], "-ener") && strstr (argv[i], "in") && strstr(argv[i],"in")  && strstr(argv[i],"name"))
      {
         energy_in_name = argv[i+1];
         i += 2;
      }
      else if (strstr (argv[i], "-e") && strstr (argv[i], "vect") && strstr(argv[i],"in")  && strstr(argv[i],"name"))
      {
         evector_in_name = argv[i+1];
         i += 2;
      }
      else if (strstr (argv[i], "-dir"))
      {
         dirname = argv[i+1];
         i += 2;
      }
      else if (strstr (argv[i], "-e") && strstr (argv[i], "vect") && strstr(argv[i],"num"))
      {
         if (sscanf(argv[i+1],"%d", &evnumber) != 1) 
         {
            printf("evnumber  messed up\n");
            exit(2); 
         }

         i += 2;
      }
      else if (strstr (argv[i], "-cut") && strstr (argv[i], "off"))
      {
         if (sscanf(argv[i+1],"%lf", &cutoff) != 1) 
         {
            printf("cutoff  messed up\n");
            exit(2); 
         }
         i += 2;
      }
      else
      {
         printf("don't understand command line: %s\n",argv[i]);
         exit(3);
      }

   }


   pm->num_sites = pm->L;
   ull * states;
   if (calc_eigenvectors)
   {
      states = enumerate_r_basis(pm->L, pm->num_particles);
      int numstatess = size_(states);
      //printf("numstatess:%d",numstatess);
      int numstates = size_(states);
      pm->numstates = numstates;
      pm->rbasis = numstates;
      double * h = energy(states, pm);

      double * e = diag(h,numstates,pm);
      write_efiles(pm, h,  e);
   }
   if (0 && calc_multi)
   {
      FILE * evectors_in = read_header(evector_in_name, pm);
      pm->rbasis = pm->numstates;
      double ** evectors;
      newarr_(evectors, pm->rbasis);
      int j;
      for(j=0; j < pm->rbasis;j++)
      {
         evectors[j] = read_evector(evectors_in, pm);
      }
      //hist_evector(pm, evectors[pm->rbasis/2]);
      hist_evector(pm, evectors[0],0);
   }
   char nameD[128];
   sprintf(nameD, "file_Pmax.d");
   FILE * file_Pmax = fopen(nameD,"w");
   int uu;

   sprintf(nameD, "file_Smin.d");
   FILE * file_Smin = fopen(nameD,"w");

   if (monte_carlo)
   {
      double E=0, E_init=0, S_init=0, S_final=0;
      _Complex double * psi_init;
      PARAMS pm_in;
      double * evectors= read_evectors(evector_in_name, &pm_in);
      double * energy = read_energies(energy_in_name,&pm_in);
      compare_pms(&pm_in,pm);
      states = enumerate_r_basis(pm->L, pm->num_particles);
      numstates = size_(states);
      pm->numstates = numstates;
      psi_init = psi_thermal(numstates,beta,energy,evectors, &E_init);
      CG * cg = create_CG(pm, size_of_box, numstates);
      makeEindices(cg, numCoarseEs, numstates, energy);
      _Complex double * c = coeff(psi_init, size_(psi_init), evectors);
      //EG * eg = energy_cell_evectors(pm, size_of_box);
      EG * eg = 0;
      double monte =  monte_func(pm, cg, c, evectors, energy, eg, psi_init);
      printf("ACCEPTANCE RATIO:%lf\n",monte);}


for(uu=0; uu <1;uu++){


   if (evolve_system)
   {
      PARAMS pminit = pm[0];
      pminit.L = pm->Linit;
      ull * states_init = enumerate_r_basis(pm->Linit, pm->num_particles);
      int numstates = size_(states_init);
      printf("Initial number of states = %d\n", numstates);
      printf("Time = %f\n",time);
      pminit.numstates = numstates;
      double * hinit = energy(states_init, &pminit);//construct hamiltonian matrix
      double * einit = diag(hinit,numstates,&pminit);//hinit now contains eigenvectors,einit energy eigenvalues
      double E=0, E_init=0, S_init=0, S_final=0;
      _Complex double * psi_init;
      THERMO * thermo1 = calc_thermo(einit, numstates, delta_beta);
      if (1)
      {
         if (beta < 0)
         {
            psi_init = convert_double2complex(hinit+numstates*evnumber,numstates);
            E = einit[evnumber];
            int beta_index1 =  calc_beta_index(thermo1, E);
            S_init = thermal_entropy(thermo1, E, (int) beta_index1);
         }
         else
         {
            psi_init = psi_thermal(numstates,beta,einit,hinit, &E_init);
            //S_init = S_of_beta(beta, einit, numstates);
            int beta_index1 =  calc_beta_index(thermo1, E_init);
            S_init = thermal_entropy(thermo1, E_init, beta_index1);
         }
      }
      states = enumerate_r_basis(pm->L, pm->num_particles);
      numstates = size_(states);
      printf("Final number of states = %d\n", numstates);
      //FILE * evectors_in = read_header(evector_in_name, pm);
      PARAMS pm_in;
      double * evectors= read_evectors(evector_in_name, &pm_in);
      double * energy = read_energies(energy_in_name,&pm_in);
      compare_pms(&pm_in,pm);
      THERMO * thermo2 = calc_thermo(energy, numstates, delta_beta);
      int beta_index2;
      if (1)
      {
         if (beta > 0)
         {
            beta_index2 =  calc_beta_index(thermo2, E_init);
            S_final = thermal_entropy(thermo2, E_init, beta_index2);
         }
         else
         {
            beta_index2 =  calc_beta_index(thermo2, E);
            S_final = thermal_entropy(thermo2, E, beta_index2);
         }
      }
      printf("S_init = %lf, S_final = %lf, deltaS = %lf\n",S_init, S_final, S_final-S_init);

      pm->numstates = numstates;
      pm->num_bath_sites = pminit.num_bath_sites;//read_header overwrote old params in pm.
      if (pm->Linit  == 0) pm->Linit = pminit.L;
      EG * eg = energy_cell_evectors(pm, size_of_box);
      _Complex double * c_init = coeff(psi_init, size_(psi_init), hinit);
      freearr_(psi_init);
      psi_init = psit(c_init,hinit,einit,t_shift);
      _Complex double * psi_init_conv = convert1to2(pm, psi_init, pm->Linit, states_init, pm->L, states);
      _Complex double * c = coeff(psi_init_conv, size_(psi_init_conv), evectors);
      _Complex double * psi = psit(c,evectors,energy,time);

      CG * cg = create_CG(pm, size_of_box, numstates);
      makeEindices(cg, numCoarseEs, numstates, energy);

      double t;
      //char nameD[128];
      printf("Dominik entropy:\n time, S_obs\n");
      sprintf(nameD, "dom_ent__t%1.1f_tprime%1.2f_V%1.2f_Vp%1.2f_L%d_Ne%dsize_box%dnumCEs%d_B%1.2f_uu%d.d", pm->t[0], pm->t[1],pm->U,pm->Up,pm->L,pm->num_particles,size_of_box,numCoarseEs,beta,uu);
      FILE * dom_ent = fopen(nameD,"w");
      sprintf(nameD, "S_E_t%1.1f_tprime%1.2f_V%1.2f_Vp%1.2f_L%d_Ne%dsize_box%dnumCEs%d_B%1.2f_uu%d.d", pm->t[0], pm->t[1],pm->U,pm->Up,pm->L,pm->num_particles,size_of_box,numCoarseEs,beta,uu);
      FILE * file_S_E = fopen(nameD,"w");
      sprintf(nameD, "ent_entr_vs_t%1.1f_tprime%1.2f_V%1.2f_Vp%1.2f_L%d_Ne%dsize_box%dnumCEs%d_B%1.2f_uu%d.d", pm->t[0], pm->t[1],pm->U,pm->Up,pm->L,pm->num_particles,size_of_box,numCoarseEs,beta,uu);
      FILE * ent_entr_vs_t = fopen(nameD,"w");
      FILE * file_Ps;


      if (print_dens) file_Ps = fopen("Ps.json","w");
      //ull ** regions =  calc_regions( pm);
      int k;
      int tot_its = time/delta_t; 
      int num_x = cg->L/cg->size_of_box;
      //fprintf(file_Ps,"[%d,%d,%d],\n[",num_x,cg->numCoarseEs,tot_its);
      if (print_dens) fprintf(file_Ps,"[");

      //psi_e_b = transform_pos_to_energy(eg, psi);//pass on size of box as
      //parameter and calculate eg inside min.c since it depedns on psi given a particular phase  
      int y, g, gg, ggg;
      //sprintf(nameD, "file_Smin.d");
      //FILE * file_Smin = fopen(nameD,"w");
       
      //calculating <E> from psi1
      int ii;
      double expE=0;
      for(ii=0;ii<pm->numstates;ii++){
      expE += energy[ii]*SQR(cabs(c[ii]));}
      //printf("mean energy:\n");
      //printf("%lf\n",expE);
      
      //unitary minimization:
      if (minimize_unitary){
         double min_P =  unitary_min(cg, pm, c, evectors,energy,eg);
         //printf("%lf\n",min_P);
         printf("Pmax: %lf\n",min_P);
         fprintf(file_Pmax,"%lf\n", min_P);}
      //fclose(file_Pmax);

      if (0 && monte_carlo){
         double monte =  monte_func(pm, cg, c, evectors, energy, eg, psi_init);
         printf("ACCEPTANCE RATIO:%lf\n",monte);}


      if (minimize)  
      {

         if (0 && minimize_unitary)
         {
            double min_P =  unitary_min(cg, pm, c, evectors, energy, eg);
            printf("min_P = %lf\n",min_P);
         }

         else

         {
            printf("uu=%d\n",uu);
            //if ((uu==3)||(uu==4)||(uu==5)){
            //if (uu==5){
            for (g=0; g <1 ; g++){
               double sfval = 0;
               sfval = Entropy_min(pm, cg, c, evectors, energy, eg, psi_init);
               fprintf(file_Smin,"%lf\n", -sfval);}
            //fclose(file_Smin);
            //}
         }  
         }
         for (ggg=0; ggg < 1; ggg++){
            ull ** reg = calc_regions_x(pm);
            if (maximize_reg_prob) regional_prob_max(pm, cg, c, evectors, energy, reg);
            freearr_(reg);}

         for(k=0; k < tot_its; k++)
         {
            double S_o=0,S_E=0;
            _Complex double * psi_energy_basis=0;
            t = delta_t*k;
            psi = psit(c,evectors,energy,t);
            int fc,f=0;
            
	    //ull * binary_basis_0 = enumerate_r_basis(pm->num_sites,pm->num_particles);
	    //double * density_matrix_t = den(pm, psi,binary_basis_0);
	    //printf("%lf\n",density_matrix_t[2]); 
	    
	    //printf("t==%f\n",t);
            //printing out psi at every time step: 
            /*
               for(f=0;f<pm->numstates;f++)
               {
               if (f==0)  printf("[");
               double re=creal(psi[f]);
               double im=cimag(psi[f]); 
               if (f==pm->numstates-1)  printf(" %.2f %+.2fj],",re,im);
               else  printf(" %.2f %+.2fj,",re,im);}

               printf("\n");


               if (t==0.0){
               printf("t==%f\n",t);
               for(fc=0;fc<pm->numstates;fc++){
               double rec=creal(c[fc]);
               double imc=cimag(c[fc]);
               printf(" %.2f %+.2fj,",rec,imc);}
               printf("\n");
               } 

            ull * binary_basis_0 = enumerate_r_basis(pm->num_sites,pm->num_particles);
            double * density_matrix_t = den(pm, psi,binary_basis_0);
            printf("density_S[t]:\n");
            for (int index=0;index < pm->L;index++){
            printf("%lf\n",density_matrix_t[index]);}
	    printf("%lf\n",density_matrix_t[0]); 
            
	    //calculating Sent at t=0
            double S_ent_corres = calc_ent_entropy_one_ev_complex_(psi, pm,pm->num_bath_sites); 
            printf("S_ent(t==0) = %lf\n",S_ent_corres);

            //computing all binary and corresponding probabilities:
            double phi_f;
            int ii,jj;
            int N = pm->numstates;
            unsigned long long * binary_basis = enumerate_r_basis(pm->num_sites,pm->num_particles);
            double np_n0 = 0;
            double np_0n = 0;
            double np_nhalf = 0;
            double np_12 = 0;
            double np_21 = 0;
            int sn0=0;
            int s0n=0;
            int sn2=0;
            int s12=0;
            int s21=0;
            for(jj=0;jj<N;jj++){ //going over all binary basis
            double p=0;
            unsigned long b = binary_basis[jj];
            _Complex double v = 0;
            for(ii=0; ii < N ;ii++){ //going over Evector
            double * evector = evectors+N*ii;
            v+=conj(c[ii])*psi[ii]*evector[jj];}
            p += v*conj(v);

            print_binary(b, pm->num_sites);
            printf(" binary: %ld\n",b);
            //printf("P for binary %lu is = %lf\n",b,p);
            printf("%lf\n",p);
            if (num_ones_in_range(0,pm->num_sites-pm->num_bath_sites,b) == pm->num_particles){
            np_n0+=p;
            sn0+=1;}
            if (num_ones_in_range(pm->num_sites-pm->num_bath_sites,pm->num_sites,b) == pm->num_particles){
            np_0n+=p;
            s0n+=1;}
            if (num_ones_in_range(pm->num_sites-pm->num_bath_sites,pm->num_sites, b) == (pm->num_particles)/2){
            np_nhalf+=p;
            sn2+=1;}

            if (num_ones_in_range(pm->num_sites-pm->num_bath_sites,pm->num_sites,b) == 2){
            np_12+=p;
            s12+=1;}
         if (num_ones_in_range(pm->num_sites-pm->num_bath_sites,pm->num_sites,b) == 1){
            np_21+=p;
            s21+=1;} 

         }
         //printf("P|n,0>=%lf , n=%d\n",np_n0, sn0);
         //printf("P|0,n>=%lf , n=%d\n",np_0n, s0n);
         //printf("P|n/2,n/2>=%lf,n=%d\n",np_nhalf, sn2);
         //printf("P|1,2>=%lf , n=%d\n",np_12, s12);
         //printf("P|2,1>=%lf , n=%d\n",np_21, s21);

            ull * binary_basis_0 = enumerate_r_basis(pm->num_sites,pm->num_particles);
            double * density_matrix_t = den(pm, psi,binary_basis_0);
	    printf("%lf \n",density_matrix_t[15]);
            	    
         */
            if (calc_obs_xe) S_o = ObsEntropyXE(pm, cg, evectors, energy, c, t);
            else S_o = ObsEntropyEX(pm, cg, evectors, energy, psi); 
           double S_ent = calc_ent_entropy_one_ev_complex_(psi, pm, pm->num_bath_sites); 
         //}

         if (calc_FOE) 
         {
            psi_energy_basis =  transform_pos_to_energy(eg, psi);
            S_E = Sobs_fine_grain_E(psi_energy_basis);//FOE
            fprintf(file_S_E,"%lf %lf\n",t, S_E);
         }
         fprintf(dom_ent,"%lf %lf\n",t, S_o );
         fprintf(ent_entr_vs_t,"%lf %lf\n",t, S_ent); 
         if (print_dens) json_print_density(pm,cg,file_Ps);
         if (print_dens) if (k < tot_its-1) fprintf(file_Ps,",\n");
         freearr_(psi);
         freearr_(psi_energy_basis);
         free2darr_(cg->Ps);
         free2darr_(cg->density);
         //freearr_(cg->states); 
         //free2darr_(cg->c_g);
         }
         if (print_dens)  fprintf(file_Ps,"]");
         if (print_dens)  fclose(file_Ps);
         fclose(dom_ent);
         fclose(file_S_E);
         fclose(ent_entr_vs_t);
      }}
fclose(file_Pmax);
   if (calc_reduced_evs)
   {
      FILE * evectors_in = read_header(evector_in_name, pm);
      pm->rbasis = pm->numstates;
      double ** evectors;
      ull * binary_basis = enumerate_r_basis(pm->L, pm->num_particles);
      newarr_(evectors, pm->rbasis);
      int j;
      int basis;

      char name[100];
      FILE *evs;
      sprintf(name, "ev_t%1.1f_tprime%1.2f_V%1.2f_Vp%1.2f_L%d_Ne%d_strt%d_end%d.dat", pm->t[0], pm->t[1],pm->U,pm->Up,pm->L,pm->num_particles,start,end);
      evs=fopen(name,"w");

      for(j=0; j < pm->rbasis;j++)
      {
         evectors[j] = read_evector(evectors_in, pm);
      }
      for(basis=0;basis < pm->rbasis;basis++)
      {
         if (num_ones_in_range(start,end,binary_basis[basis]) == pm->num_particles)
         {
            int ev_index;
            for(ev_index=0;ev_index < pm->numstates;ev_index++)
               fprintf(evs,"%10g ",evectors[ev_index][basis]);
            fprintf(evs,"\n");
         }
      }
      fclose(evs);
   }
   if (calc_entropy)
   {
      assert(!(superpose&single_state_entropy));
      if (1)
      {
         EG * eg = energy_cell_evectors(pm, size_of_box);
         char ent_name[100];
         char foe_name[100];
         char s_ex_name[100];
         int ev_indx;
         int max_bath_sites = pm->max_bath_sites;
         double * energy = read_energies(energy_in_name,pm);
//         FILE * evectors_in = read_header(evector_in_name, pm);
         double * evectors_flat = read_evectors(evector_in_name, pm);
         if (single_state_entropy)
         {
            sprintf(foe_name, "foe_single_out_t%1.1f_tprime%1.2f_V%1.2f_Vp%1.2f_L%d_Ne%dszbx%d.dat", pm->t[0], pm->t[1],pm->U,pm->Up,pm->L,pm->num_particles,size_of_box);
            sprintf(s_ex_name, "s_ex_sngl_t%1.1f_tprime%1.2f_V%1.2f_Vp%1.2f_L%d_Ne%dszbx%dnumCEs%d.dat", pm->t[0], pm->t[1],pm->U,pm->Up,pm->L,pm->num_particles,size_of_box,numCoarseEs);
         }
         else if (superpose)
         {
            sprintf(foe_name, "foe_super_out_t%1.1f_tprime%1.2f_V%1.2f_Vp%1.2f_L%d_Ne%dszbx%dnumens%d.dat", pm->t[0], pm->t[1],pm->U,pm->Up,pm->L,pm->num_particles,size_of_box,num_in_ensemble);
            sprintf(s_ex_name, "s_ex_super_t%1.1f_tprime%1.2f_V%1.2f_Vp%1.2f_L%d_Ne%dszbx%dnumCEs%d.dat", pm->t[0], pm->t[1],pm->U,pm->Up,pm->L,pm->num_particles,size_of_box,numCoarseEs);
         }
         else
         {
            sprintf(foe_name, "foe_mic_t%1.1f_tprime%1.2f_V%1.2f_Vp%1.2f_L%d_Ne%dszbx%d.dat", pm->t[0], pm->t[1],pm->U,pm->Up,pm->L,pm->num_particles,size_of_box);
            sprintf(s_ex_name, "s_ex_mic_t%1.1f_tprime%1.2f_V%1.2f_Vp%1.2f_L%d_Ne%dszbx%dnumCEs%dnumens%d.dat", pm->t[0], pm->t[1],pm->U,pm->Up,pm->L,pm->num_particles,size_of_box,numCoarseEs,num_in_ensemble);
         }
         sprintf(ent_name, "ent_out_t%1.1f_tprime%1.2f_V%1.2f_Vp%1.2f_L%d_Ne%d.dat", pm->t[0], pm->t[1],pm->U,pm->Up,pm->L,pm->num_particles);
         FILE * fname = fopen(ent_name,"w");
         FILE * f_foe_name = fopen(foe_name,"w");
         FILE * f_s_ex_name = fopen(s_ex_name,"w");
         CG * cg = create_CG(pm, size_of_box, pm->numstates);
         makeEindices(cg, numCoarseEs, pm->numstates, energy);
         pm->rbasis = pm->numstates;
         pm->max_bath_sites = max_bath_sites;
         pm->num_in_ensemble = num_in_ensemble;
         pm->num_of_Ss = pm->rbasis/pm->num_in_ensemble;
         int i,j,k;
         double ** evectors;
         newarr_(evectors, pm->num_in_ensemble);
         _Complex double ** psi_energy_basis;
         newarr_(psi_energy_basis,pm->num_in_ensemble);
         if (single_state_entropy|superpose) newarr_(psi_energy_basis,1);
         if (0 && max_bath_sites > pm->L/2)
            max_bath_sites = pm->L/2;
         printf("num of Ss=%d, num in ensemble = %d\n", pm->num_of_Ss,pm->num_in_ensemble);


         double *  ev_start = evectors_flat;
         int position=0;
         for(i=0; i < pm->num_of_Ss; i++)
         {
            double e_ave=0;
            for(j=0; j < pm->num_in_ensemble;j++)
            {
               e_ave += energy[position++];
               cpy_pntr_to_arr_(evectors[j],ev_start,pm->numstates);
               ev_start += pm->numstates;
            }
            e_ave /= pm->num_in_ensemble;
            double * S, Sobs_fine_grain_micro, S_EX=0;
            {
               if (single_state_entropy|superpose)
               {
                  _Complex double * psi;
                  if (single_state_entropy)
                     psi = convert_double2complex(evectors[pm->num_in_ensemble/2],pm->numstates);
                  else
                  {
                     psi = superpose_psi(evectors);
                     S = calc_ent_entropy_n_evs_superpose(evectors, pm, pm->num_in_ensemble);
                  }
                  psi_energy_basis[0] =  transform_pos_to_energy(eg, psi);
                  e_ave = energy[position-pm->num_in_ensemble/2];
                  S_EX = ObsEntropyEX(pm, cg, evectors_flat, energy, psi);
                  freearr_(psi);
               }
               else
               {
                  for(k=0; k < pm->num_in_ensemble;k++)
                  {
                     _Complex double * psi = convert_double2complex(evectors[k],pm->numstates);
                     psi_energy_basis[k] =  transform_pos_to_energy(eg, psi);
                     freearr_(psi);
                  }
                  S_EX = ObsEntropyEX_micro(pm, cg, evectors_flat, energy,evectors); 
               }
               if (!superpose) S = calc_ent_entropy_n_evs(evectors, pm, pm->num_in_ensemble);
               Sobs_fine_grain_micro = Sobs_fine_grain_micro_E(psi_energy_basis);
              // S_EX = ObsEntropyEX_micro(pm, cg, evectors_flat, energy, evectors);
            }
            int num_bath_sites;
            fprintf(f_foe_name,"%lf %lf\n",e_ave, Sobs_fine_grain_micro);
            fprintf(f_s_ex_name,"%lf %lf\n",e_ave, S_EX);
            for(num_bath_sites=0; num_bath_sites <= pm->max_bath_sites;num_bath_sites++)
            {
               fprintf(fname, "%lf ", S[num_bath_sites]);
               //	      printf("%lf ", S[num_bath_sites]);
            }
            fprintf(fname, "\n");
            //	   printf("\n");

            for(j=0; j < pm->num_in_ensemble;j++)
            {
               freearr_(evectors[j]);
            }
            if (i%100 == 0)
               printf("processed %d S's\n",i);

            for(k=0; k < size_(psi_energy_basis);k++)
            {
               freearr_(psi_energy_basis[k]);
            }}

         /*
         for(k=0; k < size_(psi_energy_basis);k++)
         {
            freearr_(psi_energy_basis[k]);
         }
         */
//         fclose(evectors_in);
         fclose(fname);
         fclose(f_foe_name);
         fclose(f_s_ex_name);
      }
      else
      {
         FILE * fname = fopen("ent_out.dat","w");
         double * evectors = read_evectors(evector_in_name, pm);
         double ** S = calc_ent_entropy(evectors, pm);
         int num_bath_sites, ev_index;
         for(num_bath_sites=0; num_bath_sites < pm->L;num_bath_sites++)
         {
            for(ev_index=0; ev_index < pm->rbasis; ev_index++)
            {
               //printf("eigenvector %d: Ent entropy = %lf\n", ev_index, S[ev_index]);
               fprintf(fname, "%lf ", S[num_bath_sites][ev_index]);
            }
            fprintf(fname, "\n");
         }
         fclose(fname);
      }
   }
fclose(file_Smin);
}

#else
int main(int argc, char * argv[])
{
  srandom(1000);
  unitary_test(50,3);
}
#endif

