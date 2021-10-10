#
# CDDL HEADER START
#
# The contents of this file are subject to the terms of the
# Common Development and Distribution License (the "License").
# You may not use this file except in compliance with the License.
#
# You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
# or http://www.opensolaris.org/os/licensing.
# See the License for the specific language governing permissions
# and limitations under the License.
#
# When distributing Covered Code, include this CDDL HEADER in each
# file and include the License file at usr/src/OPENSOLARIS.LICENSE.
# If applicable, add the following below this CDDL HEADER, with the
# fields enclosed by brackets "[]" replaced with your own identifying
# information: Portions Copyright [yyyy] [name of copyright owner]
#
# CDDL HEADER END
#
#
# Copyright (c) 2008, 2010, Oracle and/or its affiliates. All rights reserved.
#
"""
DDU exception class
"""
class DDUException(Exception):
    """DDU exception"""
    pass

class RepositoryNotFoundException(DDUException):
    """No repo found"""
    def __init__(self):
        DDUException.__init__(self)
    def __str__(self):
        str_message = "No repositories found on this system"
        return(str_message)

class RepositoryCreatedException(DDUException):
    """cannot create repository"""
    def __init__(self, repo_name):
        DDUException.__init__(self)
        self.repo_name = repo_name
    def __str__(self):
        str_message = " Cannot create repository for: %s" % str(self.repo_name)
        return(str_message)

class RepositoryNotReadyException(DDUException):
    """cannot run pkg command"""
    def __init__(self):
        DDUException.__init__(self)
    def __str__(self):
        str_message = " Cannot run pkg command on your system"
        return(str_message)
    
class DevScanNotStart(DDUException):
    """cannot start probe script"""
    pass

class DevDetailNotAvailable(DDUException):
    """cannot get device inf"""
    def __init__(self, dev_path):
        DDUException.__init__(self)
        self.dev_path = dev_path
    def __str__(self):
        str_message = " Cannot get detail information from device:%s" % \
                      str(self.dev_path)
        return(str_message)

class RepositorylistNotValid(DDUException):
    """invalid repo list"""
    def __init__(self):
        DDUException.__init__(self)
    def __str__(self):
        str_message = "Repository list invalid"
        return(str_message)

class DDuDevDataNotValid(DDUException):
    """device inf invalid"""
    def __init__(self):
        DDUException.__init__(self)
    def __str__(self):
        str_message = "DevData is invalid."
        return(str_message)

class PackageNoFound(DDUException):
    """no package found"""
    def __init__(self, dev_des):
        DDUException.__init__(self)
        self.dev_des = dev_des
    def __str__(self):
        str_message = "Package for device %s not found" % self.dev_des
        return(str_message)

class PackageInvalid(DDUException):
    """package invalid"""
    def __init__(self, message):
        DDUException.__init__(self)
        self.message = message
    def __str__(self):
        str_message = "Package invalid:%s" % self.message
        return(str_message)
    
class PackageInstallNotAllowed(DDUException):
    """cannot install package"""
    def __init__(self, pkg_name):
        DDUException.__init__(self)
        self.pkg_name = pkg_name
    def __str__(self):
        str_message = "Install of package %s not allowed"  % self.pkg_name
        return(str_message)

class InstallAreaUnaccessible(DDUException):
    """install area unaccessible"""
    def __init__(self, pkg_location):
        DDUException.__init__(self)
        self.pkg_location = pkg_location
    def __str__(self):
        str_message = "Installation area %s inaccessible"  % self.pkg_location
        return(str_message)

class InstallPkgFail(DDUException):
    """install PKG package fail"""
    def __init__(self, pkg_name):
        DDUException.__init__(self)
        self.pkg_name = pkg_name
    def __str__(self):
        str_message = "Installation of pkg(5) package %s failed"  % \
                                    self.pkg_name
        return(str_message)

class InstallUnkFail(DDUException):
    """install UNK package fail"""
    def __init__(self, pkg_type, pkg_name):
        DDUException.__init__(self)
        self.pkg_type = pkg_type
        self.pkg_name = pkg_name
    def __str__(self):
        str_message = "Installation of %s type package %s failed"  % \
                                    (self.pkg_type, self.pkg_name)
        return(str_message)

