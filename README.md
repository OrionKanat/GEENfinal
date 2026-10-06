# GEENfinal
Github repo to host all code for the final creel counter project. Static website will be hosted through github pages

# Architecture

Esp-32 runs .ino code
passes data to postgresql through REST api, run POST requests in batch when user returns to wifi *use postGIS to handle gis data*
Website is static, built on JS and HTML(gis implementation may be easier to diy, alternatively link a host from somewhere else or something else? tbd
website fetches data from postgreSQL through rest
