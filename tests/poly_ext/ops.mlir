// RUN: heir-opt %s > %t

// This simply tests for syntax.

#my_poly = #polynomial.polynomial<1 + x**1024>
#ring1 = #polynomial.ring<cmod=7917, ideal=#my_poly>
#ring2 = #polynomial.ring<cmod=3123, ideal=#my_poly>

module {
  func.func @test_ops(%p0 : !polynomial.polynomial<#ring1>) {
    %cmod_switch = poly_ext.cmod_switch %p0 {congruence_modulus=117} : !polynomial.polynomial<#ring1> -> !polynomial.polynomial<#ring2>
    return
  }

  func.func @test_elementwise_ops(%p0 : !polynomial.polynomial<#ring1>, %p1: !polynomial.polynomial<#ring1>) {
    %tp0 = tensor.from_elements %p0, %p1 : tensor<2x!polynomial.polynomial<#ring1>>

    %cmod_switch = poly_ext.cmod_switch %tp0 {congruence_modulus=117} : tensor<2x!polynomial.polynomial<#ring1>> -> tensor<2x!polynomial.polynomial<#ring2>>

    return
  }
}
